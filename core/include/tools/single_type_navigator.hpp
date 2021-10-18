/** Detray library, part of the ACTS project (R&D line)
 *
 * (c) 2021 CERN for the benefit of the ACTS project
 *
 * Mozilla Public License Version 2.0
 */

#pragma once

#include <algorithm>
#include <iostream>

#include "core/intersection.hpp"
#include "tools/intersection_kernel.hpp"
#include "utils/enumerate.hpp"
#include "utils/indexing.hpp"

namespace detray {

/** A void inpector that does nothing.
 *
 * Inspectors can be plugged in to understand the
 * the current navigation state information.
 *
 */
struct void_inspector {
    template <typename state_type>
    void operator()(const state_type & /*ignored*/) {}
};

/** The navigator struct that is agnostic to the object/primitive type. It
 *  only requires a link to the next navigation volume in every candidate
 *  that is computed by intersection from the objects.
 *
 * It follows the structure of the Acts::Navigator:
 * a sequence of
 * - status()
 * - target()
 * [- step()]
 * calls.
 *
 * The heartbeat indicates, that the navigation is still in a valid state.
 *
 * @tparam data_container is the type of the conatiner that provides the
 *                        geometry data.
 * @tparam inspector_type is a validation inspector
 */
template <typename volume_container, typename object_container,
          typename transform_container, typename mask_container,
          typename inspector_type = void_inspector>
class single_type_navigator {

    public:
    using object_t = typename object_container::value_type;
    using link_t = typename object_t::edge_links;

    /** Navigation status flag */
    enum navigation_status : int {
        e_on_target = -3,
        e_abort = -2,
        e_unknown = -1,
        e_towards_object = 0,
        e_on_object = 1,
    };

    /** Navigation trust level */
    enum navigation_trust_level : int {
        e_no_trust = 0,    // re-evalute the candidates all over
        e_fair_trust = 1,  // re-evaluate the distance & order of the
                           // (preselected) candidates
        e_high_trust = 3,  // re-evaluate the distance to the next candidate
        e_full_trust = 4   // trust fully: Don't re-evaluate
    };

    /** A nested navigation kernel struct that holds the current candiates.
     **/
    template <template <typename> class vector_type = dvector>
    struct navigation_kernel {
        // Where are we (nullptr if we are in between objects)
        const object_t *on = nullptr;

        // Our list of candidates (intersections with object)
        vector_type<intersection> candidates = {};

        // The next best candidate
        typename vector_type<intersection>::iterator next = candidates.end();

        /** Indicate that the kernel is empty */
        bool empty() const { return candidates.empty(); }

        /** Forward the kernel size */
        size_t size() const { return candidates.size(); }

        /** Clear the kernel */
        void clear() {
            candidates.clear();
            next = candidates.end();
            on = nullptr;
        }
    };

    /** A navigation state object used to cache the information of the
     * current navigation stream. These can be read or set in between
     * navigation calls.
     *
     * It requires to have a scalar represenation to be used for a stepper
     **/
    class state {
        friend class single_type_navigator;

        public:
        /** Scalar representation of the navigation state,
         * @returns distance to next
         **/
        scalar operator()() const { return distance_to_next; }

        /** Current kernel */
        const auto &nav_kernel() { return kernel; }

        /** Current candidates */
        const auto &candidates() { return kernel.candidates; }

        /** Current volume */
        const auto &volume() { return volume_index; }

        /** Current volume */
        void set_initial_volume(dindex initial_volume) {
            volume_index = initial_volume;
        }

        /** Tolerance to determine of we are on object */
        const auto &tolerance() { return on_object_tolerance; }

        /** Adjust the on-object tolerance */
        void set_tolerance(scalar tol) { on_object_tolerance = tol; }

        /** get the navigation inspector */
        const auto &nav_inspector() { return inspector; }

        /** Current navigation status */
        const auto &nav_status() { return status; }

        /** Current object the navigator is on (might be invalid if between
         * objects)
         */
        const auto &on_object() { return object_index; }

        /** The links (next volume, next object finder) of current
         * candidate
         */
        auto &nav_links() { return links; }

        /** Navigation trust level */
        const auto &nav_trust_level() { return trust_level; }

        /** Navigation trust level */
        void set_trust_level(navigation_trust_level lvl) { trust_level = lvl; }

        private:
        /** Navigation state cannot be recovered from. Leave the other
         * data for inspection.
         *
         * @return navigation heartbeat
         */
        bool abort() {
            status = e_abort;
            trust_level = e_no_trust;
            return false;
        }

        /** Navigation reaches target or leaves detector world. Stop navigation.
         *
         * @return navigation heartbeat
         */
        bool exit() {
            status = e_on_target;
            trust_level = e_full_trust;
            return true;
        }

        /** Kernel for the objects */
        navigation_kernel<> kernel;

        /** Volume we are currently navigating in */
        dindex volume_index = dindex_invalid;

        /**  Distance to next - will be cast into a scalar with call operator
         */
        scalar distance_to_next = std::numeric_limits<scalar>::infinity();

        /** The on object tolerance - permille */
        scalar on_object_tolerance = 1e-3;

        /** The inspector type of this navigation engine */
        inspector_type inspector;

        /**  The navigation status */
        navigation_status status = e_unknown;

        /** Index of a object (surface/portal) if is reached, otherwise
         * invalid
         */
        dindex object_index = dindex_invalid;

        // Point to the next volume and object finder
        link_t links = {};

        /** The navigation trust level */
        navigation_trust_level trust_level = e_no_trust;
    };

    /** Constructor with move constructor
     *
     * @param data the container for all data: volumes, primitives (objects
     *  i.e. surfaces and portals), transforms and masks (in a tuple by
     *  type)
     */
    single_type_navigator(const volume_container &volumes,
                          const object_container &objects,
                          const transform_container &transforms,
                          const mask_container &masks)
        : _volumes(volumes),
          _objects(objects),
          _transforms(transforms),
          _masks(masks) {}

    /** Navigation status() call which established the current navigation
     *  information.
     *
     * @param navigation [in, out] is the navigation state object
     * @param track [in] is the track infromation
     *
     * @return a heartbeat to indicate if the navigation is still alive
     **/
    template <typename track_t>
    bool status(state &navigation, const track_t &track) const {

        bool heartbeat = true;

        // Retrieve the volume & set index.
        const auto &volume = _volumes[navigation.volume_index];

        // If there is no_trust (e.g. at the beginning of navigation), the
        // kernel will be initialized. Otherwise the candidates are
        // re-evaluated based on current trust level
        update_kernel(navigation, track, volume.full_range());
        // Should never be the case after update call (without portals we are
        // trapped)
        if (navigation.kernel.empty()) {
            return navigation.abort();
        }
        // Did we hit a portal? (kernel needs to be re-initialized next time)
        check_volume_switch(navigation);
        navigation.inspector(navigation);

        return heartbeat;
    }

    /** Target function of the navigator, finds the next candidates
     *  and set the distance to next
     *
     * @param navigation is the navigation state
     * @param track is the current track information
     *
     * @return a heartbeat to indicate if the navigation is still alive
     **/
    template <typename track_t>
    bool target(state &navigation, const track_t &track) const {
        bool heartbeat = true;

        // FWe are already on the right track, nothing left to do
        if (navigation.trust_level == e_full_trust) {
            return heartbeat;
        }

        if (is_exhausted(navigation.kernel)) {
            navigation.kernel.clear();
            navigation.trust_level = e_no_trust;
        }

        // Retrieve the current volume
        const auto &volume = _volumes[navigation.volume_index];
        update_kernel(navigation, track, volume.full_range());

        // Should never be the case after update call
        if (navigation.kernel.empty()) {
            return navigation.abort();
        }
        // Did we hit a portal? (kernel needs to be
        // re-initialized next time)
        check_volume_switch(navigation);
        navigation.inspector(navigation);
        return heartbeat;
    }

    /** Helper method to intersect all objects of a surface/portal store
     *
     * @tparam range the type of range in the detector data containers
     *
     * @param navigation [in, out] navigation state that contains the kernel
     * @param track the track information
     * @param obj_range the surface/portal index range in the detector cont
     * @param on_object ignores on object solution
     *
     */
    template <typename track_t, typename range_t>
    void initialize_kernel(state &navigation, const track_t &track,
                           const range_t &obj_range,
                           bool on_object = false) const {

        // Get the number of candidates & run them through the kernel
        navigation.kernel.candidates.reserve(obj_range[1] - obj_range[0]);

        // Loop over all indexed objects, intersect and fill
        // @todo - will come from the local object finder
        for (size_t obj_idx = obj_range[0]; obj_idx < obj_range[1]; obj_idx++) {
            // Get next object
            const auto &obj = _objects[obj_idx];

            // Retrieve candidate from the object
            auto sfi = intersect(track, obj, _transforms, _masks,
                                 navigation.nav_links());

            // Candidate is invalid if it oversteps too far (this is neg!)
            if (sfi.path < track.overstep_tolerance) {
                continue;
            }
            // Accept if inside, but not the object we are already on
            if (sfi.status == e_inside and obj_idx != navigation.object_index) {
                navigation.status = e_towards_object;
                // object the candidate belongs to
                sfi.index = obj_idx;
                // the next volume if we encounter the candidate
                sfi.link = obj.edge()[0];
                navigation.kernel.candidates.push_back(sfi);
            }
        }
        // Prepare for evaluation of candidates
        sort_and_set(navigation);
    }

    /** Helper method to the update the next candidate intersection
     *
     * @tparam range the type of range in the detector data containers
     *
     * @param navigation [in, out] navigation state that contains the kernel
     * @param track the track information
     * @param obj_range the surface/portal index range in the detector cont
     *
     * @return A boolean condition if kernel is exhausted or not
     */
    template <typename track_t, typename range_t>
    void update_kernel(state &navigation, const track_t &track,
                       const range_t &obj_range) const {

        if (navigation.trust_level == e_no_trust) {
            // This kernel cannot be trusted
            initialize_kernel(navigation, track, obj_range);
            return;
        }
        // Update current candidate, or step further
        // - do this only when you trust level is high
        else if (navigation.trust_level >= e_high_trust) {
            while (not is_exhausted(navigation.kernel)) {
                // Only update the last intersection
                dindex obj_idx = navigation.kernel.next->index;
                const auto &obj = _objects[obj_idx];
                auto sfi = intersect(track, obj, _transforms, _masks,
                                     navigation.nav_links());
                sfi.index = obj_idx;
                sfi.link = obj.edge()[0];

                // Don't add surface you are already on
                if (sfi.status == e_inside and
                    obj_idx != navigation.object_index) {
                    // Update the intersection with a new one
                    (*navigation.kernel.next) = sfi;
                    navigation.distance_to_next = sfi.path;

                    // We may be on object (trust level is high)
                    if (std::abs(sfi.path) < navigation.on_object_tolerance) {
                        navigation.object_index = obj_idx;
                        navigation.status = e_on_object;
                        navigation.trust_level = e_high_trust;
                    }
                    // we are certainly not on object
                    else {
                        navigation.status = e_towards_object;
                        // Trust fully again
                        navigation.trust_level = e_full_trust;
                    }
                    // Don't sort again when coming from high trust
                    return;
                }
                // If not inside: increase and switch to next
                ++navigation.kernel.next;
            }
        }
        // Loop over all candidates and intersect again all candidates
        // - do this when your trust level is low
        else if (navigation.trust_level == e_fair_trust/* or
                 is_exhausted(navigation.kernel)*/) {
            for (auto &candidate : navigation.kernel.candidates) {
                dindex obj_idx = candidate.index;
                auto &obj = _objects[obj_idx];
                auto sfi = intersect(track, obj, _transforms, _masks,
                                     navigation.nav_links());
                candidate = sfi;
                candidate.index = obj_idx;
                candidate.link = navigation.nav_links()[0];
            }
            sort_and_set(navigation);
            return;
        }
        // If we end here, something went seriously wrong
        navigation.abort();
    }

    /** Helper method to sort within the kernel
     *
     * @param navigation [in, out] navigation state that contains the kernel
     */
    void sort_and_set(state &navigation) const {

        auto &kernel = navigation.kernel;
        // Sort and set distance to next & navigation status
        if (not kernel.candidates.empty()) {
            navigation.trust_level = e_full_trust;
            std::sort(kernel.candidates.begin(), kernel.candidates.end());

            // beginning of navigation with this kernel
            kernel.next = kernel.candidates.begin();

            // Are we still on object? Then goto the next cand.
            // This also excludes adjacent portals -> we are towards obj again
            if (navigation.distance_to_next < navigation.on_object_tolerance) {
                // The portal we are on automatically
                navigation.object_index = kernel.next->index;
                // The next object that should be on target
                ++navigation.kernel.next;
                navigation.trust_level = e_high_trust;
            }
            // No current object
            else {
                navigation.object_index = dindex_invalid;
            }

            navigation.status = e_towards_object;
            navigation.distance_to_next = kernel.next->path;
            return;
        }
        // If after full evaluation no candidates are there, abort
        navigation.abort();
    }

    /** Helper method to check and perform a volume switch
     *
     * @param navigation is the navigation state
     *
     * @return a flag if the volume navigation still has a heartbeat
     */
    void check_volume_switch(state &navigation) const {
        // Check if we need to switch volume index and (re-)initialize
        if (navigation.status == e_on_object and
            navigation.volume_index != navigation.kernel.next->link) {

            // Set volume index to the next volume provided by the object
            navigation.volume_index = navigation.kernel.next->link;
            navigation.kernel.clear();
            navigation.trust_level = e_no_trust;

            // We reached the end of the detector world
            if (navigation.volume_index == dindex_invalid) {
                navigation.exit();
            }
        }
    }

    /** Helper method to check if a kernel is exhaused
     *
     * @param kernel the kernel to be checked
     *
     * @return true if the kernel is exhaused
     */
    bool is_exhausted(const navigation_kernel<> &kernel) const {
        return (kernel.next == kernel.candidates.end());
    }

    private:
    /// the containers for all data
    const volume_container &_volumes;
    const object_container &_objects;
    const transform_container &_transforms;
    const mask_container &_masks;
};

}  // namespace detray