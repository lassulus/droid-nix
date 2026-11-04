package us.lassul.droidnix;

import android.app.Application;
import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioTrack;
import android.system.ErrnoException;
import android.system.Os;
import android.util.Log;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;

/**
 * Sound for the shells, which have no /dev/snd: plays what is written to the
 * pipe files/audio.fifo, raw PCM, signed 16 bit little endian, 48 kHz stereo.
 * The writer is PipeWire's module-pipe-tunnel (modules/services/pipewire.nix).
 */
final class AudioPipe {

    static void start(Application app) {
        File fifo = new File(app.getFilesDir(), "audio.fifo");
        new Thread(() -> {
            try {
                if (!fifo.exists()) Os.mkfifo(fifo.getPath(), 0600);
                AudioTrack track = new AudioTrack.Builder()
                    .setAudioAttributes(new AudioAttributes.Builder()
                        .setUsage(AudioAttributes.USAGE_MEDIA)
                        .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                        .build())
                    .setAudioFormat(new AudioFormat.Builder()
                        .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                        .setSampleRate(48000)
                        .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO)
                        .build())
                    .setTransferMode(AudioTrack.MODE_STREAM)
                    .build();
                track.play();
                byte[] buffer = new byte[16384];
                while (true) {
                    // Blocks until there is a writer; read returns -1 once it is gone.
                    try (FileInputStream in = new FileInputStream(fifo)) {
                        int have = 0, n;
                        while ((n = in.read(buffer, have, buffer.length - have)) > 0) {
                            have += n;
                            // AudioTrack only takes whole frames (4 bytes).
                            int frames = have - have % 4;
                            track.write(buffer, 0, frames);
                            System.arraycopy(buffer, frames, buffer, 0, have - frames);
                            have -= frames;
                        }
                    }
                }
            } catch (IOException | ErrnoException e) {
                Log.e("droid-nix", "audio", e);
            }
        }, "audio").start();
    }

}
