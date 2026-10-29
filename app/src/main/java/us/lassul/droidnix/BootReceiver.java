package us.lassul.droidnix;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

import java.io.File;
import java.nio.file.Files;

/**
 * Starts the services of the active generation (environment.loginHook:
 * sshd, the wake lock, ...) at boot, without a session. Before the first
 * start has switched to a generation there is nothing to start.
 */
public final class BootReceiver extends BroadcastReceiver {

    @Override
    public void onReceive(Context context, Intent intent) {
        if (!Intent.ACTION_BOOT_COMPLETED.equals(intent.getAction())) return;
        // The link to the active generation (modules/build/activation.nix); its target is in the launcher's /nix.
        if (!Files.isSymbolicLink(new File(Shell.usr(context), "nix/var/nix/profiles/droid-nix").toPath())) return;
        context.startForegroundService(new Intent(TerminalService.ACTION_BOOT).setClass(context, TerminalService.class));
    }

}
