/** Detray library, part of the ACTS project
 *
 * (c) 2022-2024 CERN for the benefit of the ACTS project
 *
 * Mozilla Public License Version 2.0
 */

#pragma once

// Project include(s)
#include "detray/definitions/algebra.hpp"
#include "detray/definitions/detail/qualifiers.hpp"

namespace detray {

/// Projection into a 3D cartesian coordinate frame
template <concepts::algebra algebra_t>
struct cartesian3D {

    using algebra_type = algebra_t;
    using scalar_type = dscalar<algebra_t>;
    using point2_type = dpoint2D<algebra_t>;
    using point3_type = dpoint3D<algebra_t>;
    using vector3_type = dvector3D<algebra_t>;
    using transform3_type = dtransform3D<algebra_t>;

    /// Local point type in 3D cartesian coordinates
    using loc_point = point3_type;

    /// This method transforms a point from a global cartesian 3D frame to a
    /// local 3D cartesian point
    DETRAY_HOST_DEVICE
    static inline point3_type global_to_local_3D(const transform3_type &trf,
                                                 const point3_type &p,
                                                 const vector3_type &dir) {
        return cartesian3D<algebra_t>::global_to_local(trf, p, dir);
    }

    /// This method transforms a point from a global cartesian 3D frame to a
    /// local 3D cartesian point
    DETRAY_HOST_DEVICE
    static inline loc_point global_to_local(const transform3_type &trf,
                                            const point3_type &p,
                                            const vector3_type & /*dir*/) {
        return trf.point_to_local(p);
    }

    /// This method transforms from a local 3D cartesian point to a point in
    /// the global cartesian 3D frame
    DETRAY_HOST_DEVICE static inline point3_type local_to_global(
        const transform3_type &trf, const point3_type &p) {
        return trf.point_to_global(p);
    }

    /// This method transforms from a local 3D cartesian point to a point in
    /// the global cartesian 3D frame
    template <typename mask_t>
    DETRAY_HOST_DEVICE static inline point3_type local_to_global(
        const transform3_type &trf, const mask_t & /*mask*/, const loc_point &p,
        const vector3_type & /*dir*/) {
        return cartesian3D<algebra_t>::local_to_global(trf, p);
    }

};  // struct cartesian3D

}  // namespace detray
