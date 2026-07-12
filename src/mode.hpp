#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace hyprwsmode {

    // Axis 2 of the two-axis mode model. Only meaningful inside Managed.
    enum class ManagedType : uint8_t { Tile, Stack };

    struct Managed {
        ManagedType type;
    };

    struct Floating {};

    // Axis 1 is which alternative of the variant is active.
    using Mode = std::variant<Managed, Floating>;

    // std::visit helper. Local so users can visit with lambdas cleanly:
    //   std::visit(overloaded{[](const Managed&){}, [](const Floating&){}}, mode);
    template <class... Ts>
    struct overloaded : Ts... {
        using Ts::operator()...;
    };
    template <class... Ts>
    overloaded(Ts...) -> overloaded<Ts...>;

    // User-facing strings for the socket2 event and dispatcher args. These
    // are the three names users know: "tile", "stack", "float". The
    // two-axis internal decomposition is not exposed on the wire.
    std::string_view formatMode(const Mode& m);
    std::string_view formatManagedType(ManagedType t);

    std::optional<Mode>        parseMode(std::string_view s);
    std::optional<ManagedType> parseManagedType(std::string_view s);

    // Flip a managed type to the other one. Used by both `wsmode toggle`
    // and the toggle-on-Floating edge case that updates lastManaged.
    constexpr ManagedType flip(ManagedType t) {
        return t == ManagedType::Tile ? ManagedType::Stack : ManagedType::Tile;
    }

}  // namespace hyprwsmode
