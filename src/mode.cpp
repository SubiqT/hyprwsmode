#include "mode.hpp"

namespace hyprwsmode {

    std::string_view formatManagedType(ManagedType t) {
        return t == ManagedType::Tile ? "tile" : "stack";
    }

    std::string_view formatMode(const Mode& m) {
        return std::visit(overloaded{
                              [](const Managed& mgr) { return formatManagedType(mgr.type); },
                              [](const Floating&)    { return std::string_view{"float"}; },
                          },
                          m);
    }

    std::optional<ManagedType> parseManagedType(std::string_view s) {
        if (s == "tile")  return ManagedType::Tile;
        if (s == "stack") return ManagedType::Stack;
        return {};
    }

    std::optional<Mode> parseMode(std::string_view s) {
        if (s == "tile")  return Mode{Managed{ManagedType::Tile}};
        if (s == "stack") return Mode{Managed{ManagedType::Stack}};
        if (s == "float") return Mode{Floating{}};
        return {};
    }

}  // namespace hyprwsmode
