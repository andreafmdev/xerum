import { useState } from "react";
import { m } from "motion/react";
import { Button, Select, T } from "@xerum/ui";
import { X } from "lucide-react";
import { useAudioSettings, useMidiInputs } from "../../juce/hooks";
import { useBackend } from "../../juce/provider";
import { Logo } from "./Header";

type Props = { onClose: () => void };

/**
 * Il pannello impostazioni dietro l'ingranaggio dell'header: uscita audio, MIDI, e l'errore
 * dell'ultimo cambio. Fratello minore di PresetOverlay — stesso guscio (posizionamento, sfondo,
 * Escape per chiudere), niente di nuovo da imparare per chi lo apre dopo l'altro.
 *
 * Nel plugin (`standalone` falso) i controlli audio lasciano il posto a una riga di testo: le
 * impostazioni le tiene l'host, e mostrare selettori che non fanno nulla sarebbe peggio che non
 * mostrarli — l'utente proverebbe a cambiare sample rate da dentro un VST3 e non succederebbe
 * niente, senza nessun modo di capire perché.
 */
export function SettingsOverlay({ onClose }: Props) {
  const backend = useBackend();
  const { settings, error, setOutput, setSampleRate, setBufferSize } = useAudioSettings();
  const { inputs, select } = useMidiInputs();
  // Il ripristino butta via la patch su cui si sta lavorando: un misclick non deve poterlo fare
  // da solo, quindi il bottone apre solo una conferma; il backend lo sente solo da qui.
  const [confirmingReset, setConfirmingReset] = useState(false);

  return (
    <m.div
      role="dialog"
      aria-label="Settings"
      onKeyDown={(e) => {
        if (e.key !== "Escape") return;
        // Con la conferma aperta, Escape la chiude e basta: chiudere anche il pannello
        // impostazioni nello stesso colpo sarebbe una seconda azione non richiesta.
        if (confirmingReset) setConfirmingReset(false);
        else onClose();
      }}
      initial={{ opacity: 0, scale: 0.99 }}
      animate={{ opacity: 1, scale: 1, transition: T.layerIn }}
      exit={{ opacity: 0, scale: 0.995, transition: T.layerOut }}
      className="sx-ovl absolute inset-0 z-20 flex flex-col gap-3 rounded-[14px] bg-background/94 p-3.5 backdrop-blur-sm"
    >
      <div className="flex items-center gap-3">
        <Logo>IMPOSTAZIONI</Logo>
        <span className="flex-1" />
        <Button variant="secondary" size="icon-xs" aria-label="Close settings" onClick={onClose} className="sx-hbtn rounded-control! [&_svg]:size-3.5">
          <X />
        </Button>
      </div>

      <section className="flex flex-col gap-2">
        <h3 className="text-2xs tracking-wider text-text-dim uppercase">Uscita audio</h3>
        {settings.standalone ? (
          <>
            <Select
              label="Audio output"
              value={settings.currentOutput}
              onChange={(v) => void setOutput(v)}
              options={settings.outputs.map((d) => ({ value: d.id, label: d.name }))}
            />
            <Select
              label="Sample rate"
              value={String(settings.currentSampleRate)}
              onChange={(v) => void setSampleRate(Number(v))}
              options={settings.sampleRates.map((r) => ({ value: String(r), label: `${r} Hz` }))}
            />
            <Select
              label="Buffer size"
              value={String(settings.currentBufferSize)}
              onChange={(v) => void setBufferSize(Number(v))}
              options={settings.bufferSizes.map((b) => ({ value: String(b), label: `${b} campioni` }))}
            />
            <p className="text-2xs text-text-dim">Latenza {settings.latencyMs.toFixed(1)} ms</p>
          </>
        ) : (
          <p data-testid="audio-host" className="text-2xs text-text-dim">Le gestisce l'host.</p>
        )}
      </section>

      <section className="flex flex-col gap-2">
        <h3 className="text-2xs tracking-wider text-text-dim uppercase">MIDI</h3>
        {inputs.host ? (
          <p data-testid="midi-host" className="text-2xs text-text-dim">Il MIDI arriva dall'host.</p>
        ) : (
          <Select
            label="MIDI input"
            onChange={(v) => void select(v)}
            value={
              inputs.devices.filter((d) => d.enabled).length === 1
                ? inputs.devices.find((d) => d.enabled)!.id
                : "all"
            }
            options={[
              { value: "all", label: "Tutti gli ingressi" },
              ...inputs.devices.map((d) => ({ value: d.id, label: d.name })),
            ]}
          />
        )}
      </section>

      {error && <p role="alert" className="text-2xs text-destructive">{error}</p>}

      <section className="mt-auto flex flex-col gap-2">
        <h3 className="text-2xs tracking-wider text-text-dim uppercase">Ripristino</h3>
        <Button
          variant="destructive"
          size="sm"
          aria-label="Ripristina i valori di fabbrica"
          onClick={() => setConfirmingReset(true)}
        >
          Ripristina i valori di fabbrica
        </Button>
      </section>

      {confirmingReset && (
        <div
          role="alertdialog"
          aria-label="Conferma ripristino"
          aria-describedby="reset-confirm-desc"
          className="absolute inset-0 z-30 flex flex-col items-center justify-center gap-3 rounded-[14px] bg-background/94 p-4 text-center backdrop-blur-sm"
        >
          <p id="reset-confirm-desc" className="max-w-[280px] text-xs text-muted-foreground">
            Parametri, mod matrix e passi dell'arpeggiatore tornano ai valori di fabbrica. La patch attuale va persa.
          </p>
          <div className="flex gap-2">
            <Button variant="secondary" onClick={() => setConfirmingReset(false)}>Annulla</Button>
            <Button
              variant="destructive"
              onClick={() => { setConfirmingReset(false); void backend.resetToDefaults(); }}
            >
              Ripristina
            </Button>
          </div>
        </div>
      )}
    </m.div>
  );
}
