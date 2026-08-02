#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace muplar::runtime::linux_common
{

struct DistroProfile {
    std::string id;
    std::string display_name;
    std::string package_manager;
    std::vector<std::string> terminal_paths;
    std::vector<std::string> terminal_packages;
};

inline std::string normalize_distro_id(std::string distro)
{
    std::transform(
        distro.begin(), distro.end(), distro.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (distro == "opensuse" || distro == "open-suse" || distro == "suse")
        return "opensuse";
    if (distro.empty() || distro == "generic linux")
        return "generic";
    return distro;
}

inline DistroProfile distro_profile(std::string distro)
{
    distro = normalize_distro_id(std::move(distro));

    if (distro == "ubuntu") {
        return {
            "ubuntu",
            "Ubuntu",
            "apt",
            {
                "/usr/bin/qterminal",
                "/usr/bin/foot",
                "/usr/bin/gnome-terminal",
                "/usr/bin/kgx",
                "/usr/bin/x-terminal-emulator",
                "/usr/bin/xterm",
                "/usr/bin/alacritty",
            },
            {"qterminal", "foot", "foot-terminfo", "xterm", "xwayland"},
        };
    }
    if (distro == "debian") {
        return {
            "debian",
            "Debian",
            "apt",
            {
                "/usr/bin/qterminal",
                "/usr/bin/foot",
                "/usr/bin/gnome-terminal",
                "/usr/bin/x-terminal-emulator",
                "/usr/bin/xterm",
                "/usr/bin/alacritty",
                "/usr/bin/lxterminal",
            },
            {"qterminal", "foot", "foot-terminfo", "xterm", "xwayland"},
        };
    }
    if (distro == "alpine") {
        return {
            "alpine",
            "Alpine Linux",
            "apk",
            {
                "/usr/bin/qterminal",
                "/usr/bin/foot",
                "/usr/bin/xterm",
                "/usr/bin/alacritty",
                "/usr/bin/lxterminal",
            },
            {"qterminal", "foot", "xterm", "xwayland"},
        };
    }
    if (distro == "fedora") {
        return {
            "fedora",
            "Fedora",
            "dnf",
            {
                "/usr/bin/qterminal",
                "/usr/bin/foot",
                "/usr/bin/gnome-terminal",
                "/usr/bin/kgx",
                "/usr/bin/xterm",
                "/usr/bin/alacritty",
            },
            {"qterminal", "foot", "xterm", "xorg-x11-server-Xwayland"},
        };
    }
    if (distro == "arch") {
        return {
            "arch",
            "Arch Linux",
            "pacman",
            {
                "/usr/bin/qterminal",
                "/usr/bin/foot",
                "/usr/bin/alacritty",
                "/usr/bin/gnome-terminal",
                "/usr/bin/kitty",
                "/usr/bin/xterm",
            },
            {"qterminal", "foot", "foot-terminfo", "ttf-dejavu", "xterm",
             "xorg-xwayland"},
        };
    }
    if (distro == "opensuse") {
        return {
            "opensuse",
            "openSUSE",
            "zypper",
            {
                "/usr/bin/qterminal",
                "/usr/bin/foot",
                "/usr/bin/gnome-terminal",
                "/usr/bin/konsole",
                "/usr/bin/xterm",
                "/usr/bin/alacritty",
            },
            {"qterminal", "foot", "xterm", "xwayland"},
        };
    }

    return {
        "generic",
        "Generic Linux",
        "",
        {
            "/usr/bin/qterminal",
            "/usr/bin/foot",
            "/usr/bin/xterm",
            "/usr/bin/gnome-terminal",
            "/usr/bin/alacritty",
            "/usr/bin/kitty",
            "/usr/bin/kgx",
            "/usr/bin/konsole",
            "/usr/bin/xfce4-terminal",
            "/usr/bin/lxterminal",
            "/usr/bin/mate-terminal",
        },
        {"qterminal", "foot", "xterm", "xwayland"},
    };
}

inline std::string terminal_install_hint(const DistroProfile &profile)
{
    std::string hint = profile.display_name + " terminal packages";
    if (!profile.package_manager.empty())
        hint += " via " + profile.package_manager;
    hint += ": ";

    for (size_t i = 0; i < profile.terminal_packages.size(); ++i) {
        if (i != 0)
            hint += ", ";
        hint += profile.terminal_packages[i];
    }
    return hint;
}

inline std::string os_release_content(const DistroProfile &profile)
{
    return "NAME=\"" + profile.display_name +
           "\"\n"
           "ID=" +
           profile.id +
           "\n"
           "PRETTY_NAME=\"" +
           profile.display_name +
           " (Muplar)\"\n"
           "HOME_URL=\"https://muplar.local/\"\n";
}

}  // namespace muplar::runtime::linux_common
