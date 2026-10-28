package com.termux.shared.markdown;

public final class MarkdownUtils {

    public static String getMarkdownCodeForString(String string, boolean codeBlock) {
        return codeBlock ? "```\n" + string + "\n```" : "`" + string + "`";
    }

}
