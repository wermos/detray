/** Detray library, part of the ACTS project (R&D line)
 *
 * (c) 2022-2024 CERN for the benefit of the ACTS project
 *
 * Mozilla Public License Version 2.0
 */

#pragma once

// Project include(s).
#include "detray/definitions/detail/qualifiers.hpp"
#include "detray/definitions/track_parametrization.hpp"
#include "detray/geometry/tracking_surface.hpp"
#include "detray/propagator/base_actor.hpp"
#include "detray/propagator/detail/jacobian_engine.hpp"

namespace detray {

template <typename algebra_t>
struct parameter_transporter : actor {

    struct state {};

    /// bound to free jacobian from previous surface
    struct bound_to_free_jacobian_kernel {

        // Transformation matching this struct
        using transform3_type = dtransform3D<algebra_t>;

        template <typename mask_group_t, typename index_t,
                  typename propagator_state_t>
        DETRAY_HOST_DEVICE inline bound_to_free_matrix<algebra_t> operator()(
            const mask_group_t& mask_group, const index_t& index,
            const transform3_type& trf3, propagator_state_t& propagation) {

            using frame_t = typename mask_group_t::value_type::shape::
                template local_frame_type<algebra_t>;

            using jacobian_engine_t = detail::jacobian_engine<frame_t>;

            return jacobian_engine_t::bound_to_free_jacobian(
                trf3, mask_group[index], propagation._stepping._bound_params);
        }
    };

    struct kernel {

        /// @name Type definitions for the struct
        /// @{

        // Transformation matching this struct
        using transform3_type = dtransform3D<algebra_t>;
        // scalar_type
        using scalar_type = dscalar<algebra_t>;
        // Matrix actor
        using matrix_operator = dmatrix_operator<algebra_t>;
        // 2D matrix type
        template <std::size_t ROWS, std::size_t COLS>
        using matrix_type = dmatrix<algebra_t, ROWS, COLS>;

        /// @}

        template <typename mask_group_t, typename index_t,
                  typename propagator_state_t>
        DETRAY_HOST_DEVICE inline void operator()(
            const mask_group_t& /*mask_group*/, const index_t& /*index*/,
            const transform3_type& trf3,
            const bound_to_free_matrix<algebra_t>& bound_to_free_jacobian,
            propagator_state_t& propagation) {

            using frame_t = typename mask_group_t::value_type::shape::
                template local_frame_type<algebra_t>;

            using jacobian_engine_t = detail::jacobian_engine<frame_t>;

            using bound_matrix_t = bound_matrix<algebra_t>;

            using free_matrix_t = free_matrix<algebra_t>;
            using free_to_bound_matrix_t =
                typename jacobian_engine_t::free_to_bound_matrix_type;

            // Stepper and Navigator states
            auto& stepping = propagation._stepping;

            // Free vector
            const auto& free_params = stepping();

            // Convert free to bound vector
            stepping._bound_params.set_parameter_vector(
                detail::free_to_bound_vector<frame_t>(trf3, free_params));

            // Free to bound jacobian at the destination surface
            const free_to_bound_matrix_t free_to_bound_jacobian =
                jacobian_engine_t::free_to_bound_jacobian(trf3, free_params);

            // Transport jacobian in free coordinate
            free_matrix_t& free_transport_jacobian = stepping._jac_transport;

            // Path correction factor
            free_matrix_t path_correction = jacobian_engine_t::path_correction(
                stepping().pos(), stepping().dir(), stepping.dtds(),
                stepping.dqopds(), trf3);

            const free_matrix_t correction_term =
                matrix_operator()
                    .template identity<e_free_size, e_free_size>() +
                path_correction;

            stepping._full_jacobian = free_to_bound_jacobian * correction_term *
                                      free_transport_jacobian *
                                      bound_to_free_jacobian;

            bound_matrix_t new_cov =
                stepping._full_jacobian * stepping._bound_params.covariance() *
                matrix_operator().transpose(stepping._full_jacobian);

            // Calculate surface-to-surface covariance transport
            stepping._bound_params.set_covariance(new_cov);
        }
    };

    template <typename propagator_state_t>
    DETRAY_HOST_DEVICE void operator()(state& /*actor_state*/,
                                       propagator_state_t& propagation) const {
        auto& stepping = propagation._stepping;
        const auto& navigation = propagation._navigation;

        // Do covariance transport when the track is on surface
        if (!(navigation.is_on_sensitive() ||
              navigation.encountered_sf_material())) {
            return;
        }

        using matrix_operator = dmatrix_operator<algebra_t>;
        using detector_type = typename propagator_state_t::detector_type;
        using geo_cxt_t = typename detector_type::geometry_context;
        const geo_cxt_t ctx{};

        // Current Surface
        const auto sf = navigation.get_surface();

        bound_to_free_matrix<algebra_t> bound_to_free_jacobian =
            matrix_operator().template zero<e_free_size, e_bound_size>();

        if (stepping._prev_sf_id != detail::invalid_value<dindex>()) {

            // Previous surface
            tracking_surface<detector_type> prev_sf{navigation.detector(),
                                                    stepping._prev_sf_id};

            bound_to_free_jacobian =
                prev_sf.template visit_mask<bound_to_free_jacobian_kernel>(
                    prev_sf.transform(ctx), propagation);
        }

        sf.template visit_mask<kernel>(sf.transform(ctx),
                                       bound_to_free_jacobian, propagation);

        // Set surface link
        stepping._bound_params.set_surface_link(sf.barcode());
    }
};  // namespace detray

}  // namespace detray
