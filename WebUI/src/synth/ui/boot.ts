// L'host ricrea l'editor a ogni apertura della finestra: la sequenza piena va suonata
// una volta sola nella vita del processo, non a ogni riapertura.
let done = false;

/** `true` alla prima chiamata del processo, `false` sempre dopo. */
export function consumeFirstBoot(): boolean {
  if (done) return false;
  done = true;
  return true;
}

/** Solo per i test. */
export function resetFirstBoot() {
  done = false;
}
