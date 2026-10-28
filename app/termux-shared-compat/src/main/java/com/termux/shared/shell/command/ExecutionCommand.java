package com.termux.shared.shell.command;

import android.net.Uri;

/** What Termux:API puts into the terminal service's execute intent. */
public final class ExecutionCommand {

    public Uri executableUri;
    public String[] arguments;
    public String runner;

    public enum Runner {
        /** In the background, without a terminal. */
        APP_SHELL("app-shell"),
        TERMINAL_SESSION("terminal-session");

        private final String name;

        Runner(String name) {
            this.name = name;
        }

        public String getName() {
            return name;
        }
    }

}
