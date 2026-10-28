package us.lassul.droidnix;

import android.app.Application;
import android.net.LocalServerSocket;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.os.Process;
import android.util.Log;

import com.termux.am.Am;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.PrintStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/**
 * The server of droid-nix-am (pkgs/android-integration/termux-am.nix), so that
 * the shells can start activities and services and send broadcasts as the
 * app: on the Unix socket files/am.sock, a client sends its arguments as
 * bash's printf %q quotes them and shuts down writing; the answer is
 * "exit code\0stdout\0stderr". Only the app's own processes may connect.
 */
final class AmServer {

    /** The bound socket; LocalServerSocket only has its fd, which closes when this is collected. */
    private static LocalSocket socket;

    static void start(Application app) {
        File path = new File(app.getFilesDir(), "am.sock");
        new Thread(() -> {
            try {
                path.delete();
                socket = new LocalSocket(LocalSocket.SOCKET_STREAM);
                socket.bind(new LocalSocketAddress(path.getPath(), LocalSocketAddress.Namespace.FILESYSTEM));
                LocalServerSocket server = new LocalServerSocket(socket.getFileDescriptor());
                while (true) {
                    LocalSocket client = server.accept();
                    new Thread(() -> serve(app, client), "am client").start();
                }
            } catch (IOException e) {
                Log.e("droid-nix", "am server", e);
            }
        }, "am server").start();
    }

    private static void serve(Application app, LocalSocket client) {
        try (LocalSocket socket = client) {
            if (socket.getPeerCredentials().getUid() != Process.myUid()) return;
            ByteArrayOutputStream request = new ByteArrayOutputStream();
            InputStream in = socket.getInputStream();
            byte[] buffer = new byte[4096];
            int n;
            while ((n = in.read(buffer)) != -1) request.write(buffer, 0, n);

            ByteArrayOutputStream stdout = new ByteArrayOutputStream();
            ByteArrayOutputStream stderr = new ByteArrayOutputStream();
            int exitCode = 0;
            try (PrintStream out = new PrintStream(stdout); PrintStream err = new PrintStream(stderr)) {
                try {
                    new Am(out, err, app).run(tokenize(request.toString(StandardCharsets.UTF_8.name())).toArray(new String[0]));
                } catch (Exception e) {
                    err.println(e);
                    exitCode = 1;
                }
            }
            socket.getOutputStream().write((exitCode + "\0" + stdout.toString(StandardCharsets.UTF_8.name())
                + "\0" + stderr.toString(StandardCharsets.UTF_8.name())).getBytes(StandardCharsets.UTF_8));
        } catch (IOException e) {
            Log.e("droid-nix", "am client", e);
        }
    }

    /** Splits what bash's printf %q makes: words with \ escapes, '...' and $'...' quotes. */
    static List<String> tokenize(String command) {
        List<String> words = new ArrayList<>();
        StringBuilder word = null;
        for (int i = 0; i < command.length(); i++) {
            char c = command.charAt(i);
            if (Character.isWhitespace(c)) {
                if (word != null) words.add(word.toString());
                word = null;
                continue;
            }
            if (word == null) word = new StringBuilder();
            if (c == '\\' && i + 1 < command.length()) {
                word.append(command.charAt(++i));
            } else if (c == '\'') {
                int end = command.indexOf('\'', i + 1);
                if (end == -1) throw new IllegalArgumentException("unterminated ' in " + command);
                word.append(command, i + 1, end);
                i = end;
            } else if (c == '$' && i + 1 < command.length() && command.charAt(i + 1) == '\'') {
                i = ansiC(command, i + 2, word);
            } else {
                word.append(c);
            }
        }
        if (word != null) words.add(word.toString());
        return words;
    }

    /** Appends the $'...' string that starts at start; returns the index of its closing quote. */
    private static int ansiC(String command, int start, StringBuilder word) {
        for (int i = start; i < command.length(); i++) {
            char c = command.charAt(i);
            if (c == '\'') return i;
            if (c != '\\' || i + 1 == command.length()) {
                word.append(c);
                continue;
            }
            char e = command.charAt(++i);
            switch (e) {
                case 'a': word.append('\007'); break;
                case 'b': word.append('\b'); break;
                case 'e': case 'E': word.append('\033'); break;
                case 'f': word.append('\f'); break;
                case 'n': word.append('\n'); break;
                case 'r': word.append('\r'); break;
                case 't': word.append('\t'); break;
                case 'v': word.append('\013'); break;
                case 'x': {
                    int end = i + 1;
                    while (end < command.length() && end < i + 3 && Character.digit(command.charAt(end), 16) != -1) end++;
                    word.append((char) Integer.parseInt(command.substring(i + 1, end), 16));
                    i = end - 1;
                    break;
                }
                default:
                    if (e >= '0' && e <= '7') {
                        int end = i;
                        while (end < command.length() && end < i + 3 && command.charAt(end) >= '0' && command.charAt(end) <= '7') end++;
                        word.append((char) Integer.parseInt(command.substring(i, end), 8));
                        i = end - 1;
                    } else {
                        word.append(e);
                    }
            }
        }
        throw new IllegalArgumentException("unterminated $' in " + command);
    }

}
