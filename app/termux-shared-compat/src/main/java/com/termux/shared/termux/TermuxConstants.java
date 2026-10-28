package com.termux.shared.termux;

/** droid-nix's package, paths and the terminal service's execute intent. */
public final class TermuxConstants {

    public static final String TERMUX_PACKAGE_NAME = "us.lassul.droidnix";
    /** Termux:API is part of the app. */
    public static final String TERMUX_API_PACKAGE_NAME = TERMUX_PACKAGE_NAME;
    public static final String TERMUX_API_APP_NAME = "droid-nix";

    public static final String TERMUX_PREFIX_DIR_PATH = "/data/data/" + TERMUX_PACKAGE_NAME + "/files/usr";
    public static final String TERMUX_BIN_PREFIX_DIR_PATH = TERMUX_PREFIX_DIR_PATH + "/bin";

    public static final class TERMUX_APP {

        public static final String TERMUX_SERVICE_NAME = TERMUX_PACKAGE_NAME + ".TerminalService";

        /** The extras of the service's execute intent (us.lassul.droidnix.TerminalService). */
        public static final class TERMUX_SERVICE {
            public static final String ACTION_SERVICE_EXECUTE = TERMUX_PACKAGE_NAME + ".service_execute";
            public static final String URI_SCHEME_SERVICE_EXECUTE = TERMUX_PACKAGE_NAME + ".file";
            public static final String EXTRA_ARGUMENTS = TERMUX_PACKAGE_NAME + ".execute.arguments";
            public static final String EXTRA_RUNNER = TERMUX_PACKAGE_NAME + ".execute.runner";
            public static final String EXTRA_BACKGROUND = TERMUX_PACKAGE_NAME + ".execute.background";
        }
    }

}
