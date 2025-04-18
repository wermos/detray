/** Detray library, part of the ACTS project
 *
 * (c) 2022-2024 CERN for the benefit of the ACTS project
 *
 * Mozilla Public License Version 2.0
 */

#pragma once

// Project include(s).
#include "detray/definitions/algebra.hpp"
#include "detray/definitions/detail/qualifiers.hpp"
#include "detray/definitions/track_parametrization.hpp"
#include "detray/geometry/coordinates/line2D.hpp"
#include "detray/propagator/detail/jacobian.hpp"

namespace detray::detail {

/// @brief Specialization for 2D cartesian frames
template <concepts::algebra algebra_t>
struct jacobian<line2D<algebra_t>> {

    /// @name Type definitions for the struct
    /// @{
    using coordinate_frame = line2D<algebra_t>;

    using algebra_type = algebra_t;
    using transform3_type = dtransform3D<algebra_t>;
    using scalar_type = dscalar<algebra_t>;
    using point3_type = dpoint3D<algebra_t>;
    using vector3_type = dvector3D<algebra_t>;

    // Rotation Matrix
    using rotation_matrix = dmatrix<algebra_t, 3, 3>;

    using bound_to_free_matrix_type = bound_to_free_matrix<algebra_t>;
    using free_to_bound_matrix_type = free_to_bound_matrix<algebra_t>;
    using free_to_path_matrix_type = free_to_path_matrix<algebra_t>;
    /// @}

    DETRAY_HOST_DEVICE
    static inline rotation_matrix reference_frame(const transform3_type &trf3,
                                                  const point3_type & /*pos*/,
                                                  const vector3_type &dir) {

        rotation_matrix rot = matrix::zero<rotation_matrix>();

        // y axis of the new frame is the z axis of line coordinate
        const vector3_type new_yaxis = trf3.z();

        // x axis of the new frame is (yaxis x track direction)
        const vector3_type new_xaxis =
            vector::normalize(vector::cross(new_yaxis, dir));

        // z axis
        const vector3_type new_zaxis = vector::cross(new_xaxis, new_yaxis);

        getter::element(rot, 0u, 0u) = new_xaxis[0];
        getter::element(rot, 1u, 0u) = new_xaxis[1];
        getter::element(rot, 2u, 0u) = new_xaxis[2];
        getter::set_block(rot, new_yaxis, 0u, 1u);
        getter::element(rot, 0u, 2u) = new_zaxis[0];
        getter::element(rot, 1u, 2u) = new_zaxis[1];
        getter::element(rot, 2u, 2u) = new_zaxis[2];

        return rot;
    }

    DETRAY_HOST_DEVICE static inline free_to_path_matrix_type path_derivative(
        const transform3_type &trf3, const point3_type &pos,
        const vector3_type &dir, const vector3_type &dtds) {

        free_to_path_matrix_type derivative =
            matrix::zero<free_to_path_matrix_type>();

        // The vector between position and center
        const point3_type center = trf3.translation();
        const vector3_type pc = pos - center;

        // The local frame z axis
        const vector3_type local_zaxis =
            getter::vector<3>(trf3.matrix(), 0u, 2u);

        // The local z coordinate
        const scalar_type pz = vector::dot(pc, local_zaxis);

        // Cosine of angle between momentum direction and local frame z axis
        const scalar_type dz = vector::dot(local_zaxis, dir);

        // Local x axis component of pc:
        const vector3_type pc_x = pc - pz * local_zaxis;

        const scalar_type norm =
            -1.f / (1.f - dz * dz + vector::dot(pc_x, dtds));

        const vector3_type pos_term = norm * (dir - dz * local_zaxis);
        const vector3_type dir_term = norm * pc_x;

        getter::element(derivative, 0u, e_free_pos0) = pos_term[0];
        getter::element(derivative, 0u, e_free_pos1) = pos_term[1];
        getter::element(derivative, 0u, e_free_pos2) = pos_term[2];
        getter::element(derivative, 0u, e_free_dir0) = dir_term[0];
        getter::element(derivative, 0u, e_free_dir1) = dir_term[1];
        getter::element(derivative, 0u, e_free_dir2) = dir_term[2];

        return derivative;
    }

    DETRAY_HOST_DEVICE
    static inline void set_bound_pos_to_free_pos_derivative(
        bound_to_free_matrix_type &bound_to_free_jacobian,
        const transform3_type &trf3, const point3_type &pos,
        const vector3_type &dir) {

        const rotation_matrix frame = reference_frame(trf3, pos, dir);

        // Get d(x,y,z)/d(loc0, loc1)
        const auto bound_pos_to_free_pos_derivative =
            getter::block<3, 2>(frame, 0u, 0u);

        getter::set_block(bound_to_free_jacobian,
                          bound_pos_to_free_pos_derivative, e_free_pos0,
                          e_bound_loc0);
    }

    DETRAY_HOST_DEVICE
    static inline void set_free_pos_to_bound_pos_derivative(
        free_to_bound_matrix_type &free_to_bound_jacobian,
        const transform3_type &trf3, const point3_type &pos,
        const vector3_type &dir) {

        const rotation_matrix frame = reference_frame(trf3, pos, dir);
        const rotation_matrix frameT = matrix::transpose(frame);

        // Get d(loc0, loc1)/d(x,y,z)
        const auto free_pos_to_bound_pos_derivative =
            getter::block<2, 3>(frameT, 0u, 0u);

        getter::set_block(free_to_bound_jacobian,
                          free_pos_to_bound_pos_derivative, e_bound_loc0,
                          e_free_pos0);
    }

    DETRAY_HOST_DEVICE
    static inline void set_bound_angle_to_free_pos_derivative(
        bound_to_free_matrix_type &bound_to_free_jacobian,
        const transform3_type &trf3, const point3_type &pos,
        const vector3_type &dir) {

        const auto local2 = coordinate_frame::global_to_local(trf3, pos, dir);

        // Reference frame
        const rotation_matrix frame = reference_frame(trf3, pos, dir);

        // New x, y and z-axis
        const vector3_type new_xaxis = getter::vector<3>(frame, 0u, 0u);
        const vector3_type new_yaxis = getter::vector<3>(frame, 0u, 1u);
        const vector3_type new_zaxis = getter::vector<3>(frame, 0u, 2u);

        // The projection of direction onto ref frame normal
        const scalar_type ipdn{1.f / vector::dot(dir, new_zaxis)};

        // d(n_x,n_y,n_z)/dPhi
        const vector3_type dNdPhi =
            getter::vector<3>(bound_to_free_jacobian, e_free_dir0, e_bound_phi);

        // Get new_yaxis X d(n_x,n_y,n_z)/dPhi
        const vector3_type y_cross_dNdPhi = vector::cross(new_yaxis, dNdPhi);

        // d(n_x,n_y,n_z)/dTheta
        const vector3_type dNdTheta = getter::vector<3>(
            bound_to_free_jacobian, e_free_dir0, e_bound_theta);

        // Build the cross product of d(D)/d(eBoundPhi) components with y axis
        auto y_cross_dNdTheta = vector::cross(new_yaxis, dNdTheta);

        const scalar_type C{ipdn * local2[0]};
        // and correct for the x axis components
        vector3_type phi_to_free_pos_derivative =
            y_cross_dNdPhi - new_xaxis * vector::dot(new_xaxis, y_cross_dNdPhi);

        phi_to_free_pos_derivative = C * phi_to_free_pos_derivative;

        vector3_type theta_to_free_pos_derivative =
            y_cross_dNdTheta -
            new_xaxis * vector::dot(new_xaxis, y_cross_dNdTheta);

        theta_to_free_pos_derivative = C * theta_to_free_pos_derivative;

        // Set the jacobian components
        getter::element(bound_to_free_jacobian, e_free_pos0, e_bound_phi) =
            phi_to_free_pos_derivative[0];
        getter::element(bound_to_free_jacobian, e_free_pos1, e_bound_phi) =
            phi_to_free_pos_derivative[1];
        getter::element(bound_to_free_jacobian, e_free_pos2, e_bound_phi) =
            phi_to_free_pos_derivative[2];
        getter::element(bound_to_free_jacobian, e_free_pos0, e_bound_theta) =
            theta_to_free_pos_derivative[0];
        getter::element(bound_to_free_jacobian, e_free_pos1, e_bound_theta) =
            theta_to_free_pos_derivative[1];
        getter::element(bound_to_free_jacobian, e_free_pos2, e_bound_theta) =
            theta_to_free_pos_derivative[2];
    }
};

}  // namespace detray::detail
