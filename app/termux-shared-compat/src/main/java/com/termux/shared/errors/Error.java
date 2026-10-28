package com.termux.shared.errors;

/** An error message. */
public final class Error {

    private final String message;

    public Error(String message) {
        this.message = message;
    }

    public String getErrorLogString() {
        return message;
    }

    @Override
    public String toString() {
        return message;
    }

}
