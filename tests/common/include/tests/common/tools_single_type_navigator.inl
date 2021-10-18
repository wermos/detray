/** Detray library, part of the ACTS project (R&D line)
 *
 * (c) 2021 CERN for the benefit of the ACTS project
 *
 * Mozilla Public License Version 2.0
 */

#include <gtest/gtest.h>

#include "core/mask_store.hpp"
#include "core/track.hpp"
#include "tests/common/read_geometry.hpp"
#include "tools/single_type_navigator.hpp"

/// @note __plugin has to be defined with a preprocessor command

auto [volumes, surfaces, transforms, discs, cylinders, rectangles] =
    toy_geometry();

// This tests the construction and general methods of the navigator
TEST(ALGEBRA_PLUGIN, single_type_navigator) {
    using namespace detray;

    /** Empty context type struct */
    struct empty_context {};

    mask_store<dtuple, dvector, decltype(discs)::value_type,
               decltype(cylinders)::value_type,
               decltype(rectangles)::value_type>
        masks;
    // populate mask store
    masks.template add_masks<0>(discs);
    masks.template add_masks<1>(cylinders);
    masks.template add_masks<2>(rectangles);

    single_type_navigator n(volumes, surfaces, transforms, masks);
    using toy_navigator = decltype(n);

    // test track
    track<empty_context> traj;
    traj.pos = {0., 0., 0.};
    traj.dir = vector::normalize(vector3{1., 1., 0.});
    traj.ctx = empty_context{};
    traj.momentum = 100.;
    traj.overstep_tolerance = -1e-4;

    toy_navigator::state state;
    state.set_initial_volume(0u);

    // Check that the state is unitialized
    // Volume is invalid
    ASSERT_EQ(state.volume(), 0u);
    // No surface candidates
    ASSERT_EQ(state.candidates().size(), 0u);
    // You can not trust the state
    ASSERT_EQ(state.nav_trust_level(),
              toy_navigator::navigation_trust_level::e_no_trust);
    // The status is unkown
    ASSERT_EQ(state.nav_status(), toy_navigator::navigation_status::e_unknown);

    //
    // beampipe
    //

    // Initial status call
    bool heartbeat = n.status(state, traj);
    // Test that the navigator has a heartbeat
    ASSERT_TRUE(heartbeat);
    // The status is towards portal (beampipe)
    ASSERT_EQ(state.nav_status(),
              toy_navigator::navigation_status::e_towards_object);
    // Now the volume, surfaces are defined and are trustworthy
    ASSERT_EQ(state.volume(), 0u);
    ASSERT_EQ(state.candidates().size(), 1u);
    ASSERT_EQ(state.nav_kernel().next->index, 2u);
    ASSERT_EQ(state.nav_trust_level(),
              toy_navigator::navigation_trust_level::e_full_trust);
    ASSERT_TRUE(std::abs(state() - 27.) < 0.01);

    // Let's immediately target, nothing should change, as there is full trust
    heartbeat = n.target(state, traj);
    // Test that the navigator has a heartbeat
    ASSERT_TRUE(heartbeat);
    // The status remains: towards portal
    ASSERT_EQ(state.nav_status(),
              toy_navigator::navigation_status::e_towards_object);
    ASSERT_EQ(state.volume(), 0u);
    ASSERT_EQ(state.candidates().size(), 1u);
    ASSERT_EQ(state.nav_kernel().next->index, 2u);
    ASSERT_EQ(state.nav_trust_level(),
              toy_navigator::navigation_trust_level::e_full_trust);
    ASSERT_TRUE(std::abs(state() - 27.) < 0.01);

    // Let's make half the step towards the portal
    traj.pos = traj.pos + 0.5 * state() * traj.dir;
    // Externally set by actor (in the future)
    state.set_trust_level(toy_navigator::navigation_trust_level::e_high_trust);
    heartbeat = n.status(state, traj);
    // Test that the navigator has a heartbeat
    ASSERT_TRUE(heartbeat);
    // The status remains: towards portal
    ASSERT_EQ(state.nav_status(),
              toy_navigator::navigation_status::e_towards_object);
    ASSERT_EQ(state.volume(), 0u);
    ASSERT_EQ(state.candidates().size(), 1u);
    ASSERT_EQ(state.nav_kernel().next->index, 2u);
    ASSERT_TRUE(std::abs(state() - 13.5) < 0.01);
    // Trust level is restored
    ASSERT_EQ(state.nav_trust_level(),
              toy_navigator::navigation_trust_level::e_full_trust);

    // Let's immediately target, nothing should change, as there is full trust
    heartbeat = n.target(state, traj);
    // Test that the navigator has a heartbeat
    ASSERT_TRUE(heartbeat);
    // The status remains: towards surface
    ASSERT_EQ(state.nav_status(),
              toy_navigator::navigation_status::e_towards_object);
    ASSERT_EQ(state.volume(), 0u);
    ASSERT_EQ(state.candidates().size(), 1u);
    ASSERT_EQ(state.nav_trust_level(),
              toy_navigator::navigation_trust_level::e_full_trust);
    ASSERT_TRUE(std::abs(state() - 13.5) < 0.01);

    // Now step onto the portal
    traj.pos = traj.pos + state() * traj.dir;
    state.set_trust_level(toy_navigator::navigation_trust_level::e_high_trust);
    heartbeat = n.status(state, traj);
    // Test that the navigator has a heartbeat
    ASSERT_TRUE(heartbeat);

    // The status is: on portal
    ASSERT_TRUE(std::abs(state()) < state.tolerance());
    ASSERT_EQ(state.nav_status(),
              toy_navigator::navigation_status::e_on_object);
    // Switched to next volume
    ASSERT_EQ(state.volume(), 1u);
    // Kernel is exhaused, and trust level is gone
    ASSERT_EQ(state.nav_kernel().next, state.candidates().end());
    ASSERT_EQ(state.nav_trust_level(),
              toy_navigator::navigation_trust_level::e_no_trust);

    //
    // layer 1
    //

    // New target call will initialize volume 1
    heartbeat = n.target(state, traj);
    // Test that the navigator has a heartbeat
    ASSERT_TRUE(heartbeat);

    // The status is: on adjacent portal in volume 1, towards next candidate
    ASSERT_EQ(state.nav_status(),
              toy_navigator::navigation_status::e_towards_object);
    ASSERT_EQ(state.volume(), 1u);
    // This includes the adjacent portal we are already on
    ASSERT_EQ(state.candidates().size(), 6u);
    // We are already on this portal, so switch to next candidate which must
    // be a surface
    ASSERT_EQ(state.nav_kernel().next->index, 128u);
    ASSERT_EQ(state.nav_trust_level(),
              toy_navigator::navigation_trust_level::e_high_trust);

    // Now step onto the surface
    traj.pos = traj.pos + state() * traj.dir;
    heartbeat = n.status(state, traj);
    // Test that the navigator has a heartbeat
    ASSERT_TRUE(heartbeat);

    // The status is: on surface 128
    ASSERT_EQ(state.nav_status(),
              toy_navigator::navigation_status::e_on_object);
    ASSERT_EQ(state.volume(), 1u);
    ASSERT_EQ(state.candidates().size(), 6u);
    ASSERT_EQ(state.nav_kernel().next->index, 128u);
    ASSERT_EQ(state.nav_trust_level(),
              toy_navigator::navigation_trust_level::e_high_trust);

    // Let's target now - update distance to next candidate in volume 1
    heartbeat = n.target(state, traj);
    // Test that the navigator has a heartbeat
    ASSERT_TRUE(heartbeat);

    ASSERT_EQ(state.nav_status(),
              toy_navigator::navigation_status::e_towards_object);
    ASSERT_EQ(state.volume(), 1u);
    // Should be on our way to the next ovelapping module
    ASSERT_EQ(state.candidates().size(), 6u);
    ASSERT_EQ(state.nav_trust_level(),
              toy_navigator::navigation_trust_level::e_full_trust);

    // Jump to the next surface
    traj.pos = traj.pos + state() * traj.dir;
    heartbeat = n.status(state, traj);
    // Test that the navigator has a heartbeat
    ASSERT_TRUE(heartbeat);

    // The status is: on surface 129
    ASSERT_EQ(state.nav_status(),
              toy_navigator::navigation_status::e_on_object);
    ASSERT_EQ(state.volume(), 1u);
    ASSERT_EQ(state.candidates().size(), 6u);
    ASSERT_EQ(state.nav_trust_level(),
              toy_navigator::navigation_trust_level::e_high_trust);

    // Let's target now - update distance to next candidate in volume 1
    heartbeat = n.target(state, traj);
    // Test that the navigator has a heartbeat
    ASSERT_TRUE(heartbeat);

    ASSERT_EQ(state.nav_status(),
              toy_navigator::navigation_status::e_towards_object);
    ASSERT_EQ(state.volume(), 1u);
    // Should be on our way to the next ovelapping module
    ASSERT_EQ(state.candidates().size(), 6u);
    ASSERT_EQ(state.nav_trust_level(),
              toy_navigator::navigation_trust_level::e_full_trust);

    // Jump to the next surface
    traj.pos = traj.pos + state() * traj.dir;
    heartbeat = n.status(state, traj);
    // Test that the navigator has a heartbeat
    ASSERT_TRUE(heartbeat);

    // The status is: on surface 112
    ASSERT_EQ(state.nav_status(),
              toy_navigator::navigation_status::e_on_object);
    ASSERT_EQ(state.volume(), 1u);
    ASSERT_EQ(state.candidates().size(), 6u);
    ASSERT_EQ(state.nav_trust_level(),
              toy_navigator::navigation_trust_level::e_high_trust);

    // Let's target now - update distance to next candidate in volume 1
    heartbeat = n.target(state, traj);
    // Test that the navigator has a heartbeat
    ASSERT_TRUE(heartbeat);

    ASSERT_EQ(state.nav_status(),
              toy_navigator::navigation_status::e_towards_object);
    ASSERT_EQ(state.volume(), 1u);
    // Should be on our way to the next ovelapping module
    ASSERT_EQ(state.candidates().size(), 6u);
    ASSERT_EQ(state.nav_trust_level(),
              toy_navigator::navigation_trust_level::e_full_trust);

    // Jump to the next surface
    traj.pos = traj.pos + state() * traj.dir;
    heartbeat = n.status(state, traj);
    // Test that the navigator has a heartbeat
    ASSERT_TRUE(heartbeat);

    // The status is: on surface 113
    ASSERT_EQ(state.nav_status(),
              toy_navigator::navigation_status::e_on_object);
    ASSERT_EQ(state.volume(), 1u);
    ASSERT_EQ(state.candidates().size(), 6u);
    ASSERT_EQ(state.nav_trust_level(),
              toy_navigator::navigation_trust_level::e_high_trust);

    // Let's target again - should go towards portal next
    heartbeat = n.target(state, traj);
    // Test that the navigator has a heartbeat
    ASSERT_TRUE(heartbeat);

    ASSERT_EQ(state.volume(), 1u);

    ASSERT_EQ(state.nav_status(),
              toy_navigator::navigation_status::e_towards_object);
    ASSERT_EQ(state.candidates().size(), 6u);
    ASSERT_EQ(state.nav_trust_level(),
              toy_navigator::navigation_trust_level::e_full_trust);

    //
    // gap volume
    //

    // Step onto the portal
    traj.pos = traj.pos + state() * traj.dir;
    heartbeat = n.status(state, traj);
    // Test that the navigator has a heartbeat
    ASSERT_TRUE(heartbeat);

    // The status is: on portal
    ASSERT_EQ(state.nav_status(),
              toy_navigator::navigation_status::e_on_object);
    // Switch to volume 2
    ASSERT_EQ(state.volume(), 2u);
    // Kernel is exhaused, and trust level is gone
    ASSERT_EQ(state.nav_kernel().next, state.candidates().end());
    ASSERT_EQ(state.nav_trust_level(),
              toy_navigator::navigation_trust_level::e_no_trust);

    // With the new target call all surfaces of vol.2 should be initialized
    heartbeat = n.target(state, traj);
    // Test that the navigator has a heartbeat
    ASSERT_TRUE(heartbeat);

    // We immediately
    ASSERT_EQ(state.volume(), 2u);

    // The status is: on adjacent portal in volume 2, towards next candidate,
    // which is portal 234
    ASSERT_EQ(state.nav_status(),
              toy_navigator::navigation_status::e_towards_object);
    // This includes the adjacent portal we are already on
    ASSERT_EQ(state.candidates().size(), 2u);
    // We are already on this portal, so switch to next candidate which must
    // be a surface
    ASSERT_EQ(state.nav_kernel().next->index, 234u);
    ASSERT_EQ(state.nav_trust_level(),
              toy_navigator::navigation_trust_level::e_high_trust);

    // Step onto the portal
    traj.pos = traj.pos + state() * traj.dir;
    heartbeat = n.status(state, traj);
    // Test that the navigator has a heartbeat
    ASSERT_TRUE(heartbeat);

    // The status is: on portal
    ASSERT_EQ(state.nav_status(),
              toy_navigator::navigation_status::e_on_object);
    // Switch to volume 3
    ASSERT_EQ(state.volume(), 3u);
    // Kernel is exhaused, and trust level is gone
    ASSERT_EQ(state.nav_kernel().next, state.candidates().end());
    ASSERT_EQ(state.nav_trust_level(),
              toy_navigator::navigation_trust_level::e_no_trust);

    //
    // layer 2
    //

    // With the new target call all surfaces of vol.3 should be initialized
    heartbeat = n.target(state, traj);
    // Test that the navigator has a heartbeat
    ASSERT_TRUE(heartbeat);

    ASSERT_EQ(state.volume(), 3u);

    // The status is: on adjacent portal in volume 3, towards next candidate,
    // which should be a module surface
    ASSERT_EQ(state.nav_status(),
              toy_navigator::navigation_status::e_towards_object);
    // This includes the adjacent portal we are already on
    ASSERT_EQ(state.candidates().size(), 6u);
    // We are already on this portal, so switch to next candidate which must
    // be a surface
    ASSERT_EQ(state.nav_kernel().next->index, 482u);
    ASSERT_EQ(state.nav_trust_level(),
              toy_navigator::navigation_trust_level::e_high_trust);

    // Now step onto the surface
    traj.pos = traj.pos + state() * traj.dir;
    heartbeat = n.status(state, traj);
    // Test that the navigator has a heartbeat
    ASSERT_TRUE(heartbeat);

    // The status is: on surface 482
    ASSERT_EQ(state.nav_status(),
              toy_navigator::navigation_status::e_on_object);
    ASSERT_EQ(state.volume(), 3u);
    ASSERT_EQ(state.candidates().size(), 6u);
    ASSERT_EQ(state.nav_kernel().next->index, 482u);
    ASSERT_EQ(state.nav_trust_level(),
              toy_navigator::navigation_trust_level::e_high_trust);

    // Let's target now - update distance to next candidate in volume 3
    heartbeat = n.target(state, traj);
    // Test that the navigator has a heartbeat
    ASSERT_TRUE(heartbeat);

    ASSERT_EQ(state.nav_status(),
              toy_navigator::navigation_status::e_towards_object);
    ASSERT_EQ(state.volume(), 3u);
    // Should be on our way to the next ovelapping module
    ASSERT_EQ(state.candidates().size(), 6u);
    ASSERT_EQ(state.nav_trust_level(),
              toy_navigator::navigation_trust_level::e_full_trust);

    // Jump to the next surface
    traj.pos = traj.pos + state() * traj.dir;
    heartbeat = n.status(state, traj);
    // Test that the navigator has a heartbeat
    ASSERT_TRUE(heartbeat);

    // The status is: on surface 483
    ASSERT_EQ(state.nav_status(),
              toy_navigator::navigation_status::e_on_object);
    ASSERT_EQ(state.volume(), 3u);
    ASSERT_EQ(state.candidates().size(), 6u);
    ASSERT_EQ(state.nav_trust_level(),
              toy_navigator::navigation_trust_level::e_high_trust);

    // Let's target now - update distance to next candidate in volume 1
    heartbeat = n.target(state, traj);
    // Test that the navigator has a heartbeat
    ASSERT_TRUE(heartbeat);

    ASSERT_EQ(state.nav_status(),
              toy_navigator::navigation_status::e_towards_object);
    ASSERT_EQ(state.volume(), 3u);
    // Should be on our way to the next ovelapping module
    ASSERT_EQ(state.candidates().size(), 6u);
    ASSERT_EQ(state.nav_trust_level(),
              toy_navigator::navigation_trust_level::e_full_trust);

    // Jump to the next surface
    traj.pos = traj.pos + state() * traj.dir;
    heartbeat = n.status(state, traj);
    // Test that the navigator has a heartbeat
    ASSERT_TRUE(heartbeat);

    // The status is: on surface 451
    ASSERT_EQ(state.nav_status(),
              toy_navigator::navigation_status::e_on_object);
    ASSERT_EQ(state.volume(), 3u);
    ASSERT_EQ(state.candidates().size(), 6u);
    ASSERT_EQ(state.nav_trust_level(),
              toy_navigator::navigation_trust_level::e_high_trust);

    // Let's target now - update distance to next candidate in volume 1
    heartbeat = n.target(state, traj);
    // Test that the navigator has a heartbeat
    ASSERT_TRUE(heartbeat);

    ASSERT_EQ(state.nav_status(),
              toy_navigator::navigation_status::e_towards_object);
    ASSERT_EQ(state.volume(), 3u);
    // Should be on our way to the next ovelapping module
    ASSERT_EQ(state.candidates().size(), 6u);
    ASSERT_EQ(state.nav_trust_level(),
              toy_navigator::navigation_trust_level::e_full_trust);

    // Jump to the next surface
    traj.pos = traj.pos + state() * traj.dir;
    heartbeat = n.status(state, traj);
    // Test that the navigator has a heartbeat
    ASSERT_TRUE(heartbeat);

    // The status is: on surface 451
    ASSERT_EQ(state.nav_status(),
              toy_navigator::navigation_status::e_on_object);
    ASSERT_EQ(state.volume(), 3u);
    ASSERT_EQ(state.candidates().size(), 6u);
    ASSERT_EQ(state.nav_trust_level(),
              toy_navigator::navigation_trust_level::e_high_trust);

    // Let's target again - should go towards portal next
    heartbeat = n.target(state, traj);
    // Test that the navigator has a heartbeat
    ASSERT_TRUE(heartbeat);

    ASSERT_EQ(state.volume(), 3u);

    ASSERT_EQ(state.nav_status(),
              toy_navigator::navigation_status::e_towards_object);
    ASSERT_EQ(state.candidates().size(), 6u);
    ASSERT_EQ(state.nav_trust_level(),
              toy_navigator::navigation_trust_level::e_full_trust);

    // Step onto the portal
    traj.pos = traj.pos + state() * traj.dir;
    heartbeat = n.status(state, traj);
    // Test that the navigator has a heartbeat
    ASSERT_TRUE(heartbeat);

    // The status is: on portal
    ASSERT_EQ(state.nav_status(),
              toy_navigator::navigation_status::e_on_target);
    // Switch to next volume leads out of the detector world -> exit
    ASSERT_EQ(state.volume(), dindex_invalid);
    ASSERT_EQ(state.nav_trust_level(),
              toy_navigator::navigation_trust_level::e_full_trust);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}
