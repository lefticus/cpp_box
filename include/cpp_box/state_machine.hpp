#ifndef CPP_BOX_STATE_MACHINE_HPP
#define CPP_BOX_STATE_MACHINE_HPP

namespace cpp_box::state_machine {

template<typename EnumType, typename Callable> struct StateTransition
{
  EnumType from;
  EnumType to;
  Callable callable;

  // behavior is undefined if callable moves a value and a future callable tries to access it
  template<typename... Param> constexpr bool test(EnumType current, Param &&... param) const
  {
    return current == from && callable(std::forward<Param>(param)...);
  }
  template<typename... Param> constexpr bool test(const EnumType current, Param &&... param)
  {
    return current == from && callable(std::forward<Param>(param)...);
  }
};

template<typename EnumType, typename Callable>
StateTransition(const EnumType, const EnumType, Callable callable)->StateTransition<EnumType, Callable>;

template<typename EnumType, typename... Callables> struct StateMachine
{
  std::tuple<StateTransition<EnumType, Callables>...> transitions;

  constexpr StateMachine(StateTransition<EnumType, Callables>... t) : transitions(std::move(t)...) {}

  template<size_t Count, size_t Index, typename Transitions, typename... Param>
  constexpr static EnumType transition_impl(const EnumType current_state, Transitions &&transitions, Param &&... params)
  {
    if constexpr (Index == Count) {
      return current_state;
    } else {
      if (std::get<Index>(std::forward<Transitions>(transitions)).test(current_state, std::forward<Param>(params)...)) {
        return std::get<Index>(std::forward<Transitions>(transitions)).to;
      } else {
        return transition_impl<Count, Index + 1>(current_state, std::forward<Transitions>(transitions), std::forward<Param>(params)...);
      }
    }
  }

  template<typename... Param> constexpr EnumType transition(const EnumType current, Param &&... param) const
  {
    return transition_impl<sizeof...(Callables), 0>(current, transitions, std::forward<Param>(param)...);
  }

  template<typename... Param> constexpr EnumType transition(const EnumType current, Param &&... param)
  {
    return transition_impl<sizeof...(Callables), 0>(current, transitions, std::forward<Param>(param)...);
  }
};

}  // namespace cpp_box::state_machine
#endif
