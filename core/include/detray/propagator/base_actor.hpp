/** Detray library, part of the ACTS project (R&D line)
 *
 * (c) 2022-2025 CERN for the benefit of the ACTS project
 *
 * Mozilla Public License Version 2.0
 */

#pragma once

// Propagate include(s)
#include "detray/definitions/containers.hpp"
#include "detray/definitions/detail/qualifiers.hpp"
#include "detray/utils/tuple_helpers.hpp"

// System include(s)
#include <concepts>
#include <type_traits>
#include <utility>

namespace detray {

/// Base class actor implementation
struct actor {
    /// Tag whether this is a composite type
    struct is_comp_actor : public std::false_type {};

    /// Defines the actors state. Hidden by actor implementations.
    struct state {};
};

namespace detail {
/// Extrac the tuple of actor states from an actor type
/// @{
// Simple actor: No observers
template <typename actor_t>
struct get_state_tuple {
    private:
    using state_t = typename actor_t::state;

    // Remove empty default state of base actor type from tuple
    using principal = std::conditional_t<std::same_as<state_t, actor::state>,
                                         dtuple<>, dtuple<state_t>>;
    using principal_ref =
        std::conditional_t<std::same_as<state_t, actor::state>, dtuple<>,
                           dtuple<state_t &>>;

    public:
    using type = principal;
    using ref_type = principal_ref;
};

// Composite actor: Has observers
template <typename actor_t>
requires(!std::same_as<typename std::remove_cvref_t<actor_t>::observer_states,
                       void>) struct get_state_tuple<actor_t> {
    private:
    using principal_actor_t = typename actor_t::actor_type;

    using principal = typename get_state_tuple<principal_actor_t>::type;
    using principal_ref = typename get_state_tuple<principal_actor_t>::ref_type;

    using observers = typename actor_t::observer_states;
    using observer_refs = typename actor_t::observer_state_refs;

    public:
    using type = detail::tuple_cat_t<principal, observers>;
    using ref_type = detail::tuple_cat_t<principal_ref, observer_refs>;
};

/// Tuple of state types
template <typename actor_t>
using state_tuple_t = get_state_tuple<actor_t>::type;

/// Tuple of references
template <typename actor_t>
using state_ref_tuple_t = get_state_tuple<actor_t>::ref_type;
/// @}

}  // namespace detail

/// Composition of actors
///
/// The composition represents an actor together with its observers. In
/// addition to running its own implementation, it notifies its observing actors
///
/// @tparam principal_actor_t the actor the compositions implements itself.
/// @tparam observers a pack of observing actors that get called on the updated
///         actor state of the compositions actor implementation.
template <class principal_actor_t = actor, typename... observers>
class composite_actor final : public principal_actor_t {

    public:
    /// Tag whether this is a composite type (hides the def in the actor)
    struct is_comp_actor : public std::true_type {};

    /// The composite is an actor in itself.
    using actor_type = principal_actor_t;
    using state = typename actor_type::state;

    /// Tuple of states of observing actors
    using observer_states =
        detail::tuple_cat_t<detail::state_tuple_t<observers>...>;
    using observer_state_refs =
        detail::tuple_cat_t<detail::state_ref_tuple_t<observers>...>;

    /// Call to the implementation of the actor (the actor possibly being an
    /// observer itself)
    ///
    /// First runs its own implementation, then passes the updated state to its
    /// observers.
    ///
    /// @param states the states of all actors in the chain
    /// @param p_state the state of the propagator (stepper and navigator)
    /// @param subject_state the state of the actor this actor observes. Uses
    ///                      a dummy type if this is not an observing actor.
    template <typename actor_states_t, typename propagator_state_t,
              typename subj_state_t = typename actor::state>
    DETRAY_HOST_DEVICE void operator()(
        actor_states_t &states, propagator_state_t &p_state,
        subj_state_t &&subject_state = {}) const {

        // State of the primary actor that is implement by this composite actor
        auto &actor_state = detail::get<typename actor_type::state &>(states);

        // Do your own work ...
        // Two cases: This is a simple actor or observing actor (pass on its
        // subject's state)
        if constexpr (std::is_same_v<subj_state_t, typename actor::state>) {
            actor_type::operator()(actor_state, p_state);
        } else {
            actor_type::operator()(actor_state, p_state,
                                   std::forward<subj_state_t>(subject_state));
        }

        // ... then run the observers on the updated state
        notify(m_observers, states, actor_state, p_state,
               std::make_index_sequence<sizeof...(observers)>{});
    }

    private:
    /// Notifies the observing actors for composite and simple actor case.
    ///
    /// @param observer one of the observers
    /// @param states the states of all actors in the chain
    /// @param actor_state the state of this compositions actor as the subject
    ///                    to all of its observers
    /// @param p_state the state of the propagator (stepper and navigator)
    template <typename observer_t, typename actor_states_t,
              typename actor_impl_state_t, typename propagator_state_t>
    DETRAY_HOST_DEVICE inline void notify(const observer_t &observer,
                                          actor_states_t &states,
                                          actor_impl_state_t &actor_state,
                                          propagator_state_t &p_state) const {
        // Two cases: observer is a simple actor or a composite actor
        if constexpr (!typename observer_t::is_comp_actor()) {
            // No actor state defined (empty)
            if constexpr (std::same_as<typename observer_t::state,
                                       detray::actor::state>) {
                observer(actor_state, p_state);
            } else {
                observer(detail::get<typename observer_t::state &>(states),
                         actor_state, p_state);
            }
        } else {
            observer(states, actor_state, p_state);
        }
    }

    /// Resolve the observer notification.
    ///
    /// Unrolls the observer types and runs the notification for each of them.
    ///
    /// @param observer_list all observers of the actor
    /// @param states the states of all actors in the chain
    /// @param actor_state the state of this compositions actor as the subject
    ///                    to all of its observers
    /// @param p_state the state of the propagator (stepper and navigator)
    template <std::size_t... indices, typename actor_states_t,
              typename actor_impl_state_t, typename propagator_state_t>
    DETRAY_HOST_DEVICE inline void notify(
        const dtuple<observers...> &observer_list, actor_states_t &states,
        actor_impl_state_t &actor_state, propagator_state_t &p_state,
        std::index_sequence<indices...> /*ids*/) const {

        (notify(detail::get<indices>(observer_list), states, actor_state,
                p_state),
         ...);
    }

    /// Keep the observers (might be composites again)
    [[no_unique_address]] dtuple<observers...> m_observers = {};
};

}  // namespace detray
