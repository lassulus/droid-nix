package com.termux.shared.theme;

/** droid-nix has no night-mode setting: dialogs follow the system. */
public enum NightMode {
    SYSTEM("system");

    private final String name;

    NightMode(String name) {
        this.name = name;
    }

    public String getName() {
        return name;
    }

    public static NightMode getAppNightMode() {
        return SYSTEM;
    }

}
