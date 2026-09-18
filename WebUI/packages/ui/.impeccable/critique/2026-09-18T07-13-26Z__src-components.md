---
target: @xerum/ui components + app shell
total_score: 22
max_score: 40
na_heuristics: 
p0_count: 2
p1_count: 2
timestamp: 2026-09-18T07-13-26Z
slug: src-components
---
# Critique — @xerum/ui componenti + app shell (2026-09-18)

Method: dual-agent (A: design review · B: detector). Direzione decisa: ibrido skeuomorfico (controlli fisici su pannelli piatti).

## Design Health Score

| # | Heuristic | Score | Key Issue |
|---|-----------|-------|-----------|
| 1 | Visibility of System Status | 2 | Valori dei knob invisibili a riposo; nessun indicatore attività voci |
| 2 | Match System / Real World | 1 | Pill switch, badge, card: nessuna metafora strumento salvo la wavetable |
| 3 | User Control and Freedom | 3 | Doppio click reset e Shift fine ottimi; nessun undo |
| 4 | Consistency and Standards | 3 | Coerente internamente; manca right-click/MIDI learn (convenzione plugin) |
| 5 | Error Prevention | 3 | Clamp 0..1; nessuna conferma su Init |
| 6 | Recognition Rather Than Recall | 2 | Readout solo su hover; A/D/S/R etichette a una lettera |
| 7 | Flexibility and Efficiency | 3 | Wheel/tastiera/Shift reali ma non scopribili |
| 8 | Aesthetic and Minimalist Design | 2 | Generico; metà viewport vuota |
| 9 | Error Recovery | 2 | Nessuno stato "bridge disconnesso" oltre al chip "stub" |
| 10 | Help and Documentation | 1 | Zero tooltip, nessun hint sulle scorciatoie |
| **Total** | | **22/40** | **Acceptable** |

## Design Specificity Verdict

LLM: dashboard shadcn con un canvas wavetable dentro. Panel, Toggle, Button, Select, ValueReadout sono primitive shadcn non modificate; niente ha massa, profondità o luce. Solo WavetableDisplay e il modello di drag sono "da synth".

Legge come AI/generico: Panel `rounded-lg border-t-2 border-t-(--tone) ring-border` (Panel.tsx:23); Toggle = pill Switch iOS; stessa etichetta `text-2xs uppercase tracking-wider` in 6 componenti; Fader = range input (track pill + thumb tondo); Knob = anello di progresso senza corpo (Knob.tsx:103–113); ValueReadout = Badge SaaS; app: gradiente hero radiale (styles.css:10) e h1 più grande di tutto; raggi dashboard (--radius .5rem ×2.2).

Ha già carattere: contratto `--tone` per sezione; geometria knob 135°/270°; useDragValue (fine mode con re-anchor, wheel, Home/End); stack di profondità della wavetable; readout knob nascosto a riposo (convenzione plugin).

Detector (B): 1 finding, `border-accent-on-rounded` in Panel.tsx:23 (concorda con A: è il "tell"). Overlay browser: saltato (nessun tool browser mutabile). Osservazioni screenshot: 4 card arrotondate con bordo colorato, 3 accent saturi, tutte le etichette uppercase tracked, >50% viewport vuoto.

## Overall Impression

Pulito e competente, visto cento volte. La singola opportunità: dare corpo ai controlli (knob, fader, toggle) e togliere le card: il resto segue.

## What's Working
1. useDragValue: modello professionale (fine mode con re-anchor a :86–91).
2. Contratto `--tone` via CSS var: scheletro giusto per la ricostruzione skeuomorfica.
3. WavetableDisplay: vocabolario synth reale, serve solo la cornice.

## Priority Issues
- [P0] Button senza sfondo/bordo/padding in Storybook e in ogni consumer di `ui.css` standalone. Causa: ordine dei cascade layer non dichiarato in index.css → `@layer base` (reset) batteva le utility. RISOLTO in questa sessione (commit 1c18e6b).
- [P0] Panel è il "tell" AI (Panel.tsx:23). Fix: piastra piatta raggio ≤4px senza ring, regola incisa (linea scura + linea chiara), tono spostato su etichetta SectionHeader + LED.
- [P1] Knob senza corpo (Knob.tsx:103–113). Fix: cappuccio con gradiente, bordo luce 1px, ombra portata, tacche grip, scala esterna con tacche min/centro/max, indicatore come solco.
- [P1] Valori nascosti fino all'hover (Knob.tsx:118). Fix: valore sempre visibile in mono sotto l'etichetta; readout flottante grande solo durante il drag.
- [P2] Uppercase ovunque appiattisce la gerarchia (6 file). Fix: uppercase+tracking solo per gli header di sezione; etichette parametro sentence-case; valori mono/tabular più chiari delle etichette.

## Persona Red Flags
Alex (power user): nessun right-click/MIDI learn; Shift-fine/wheel/doppio click non suggeriti; disabled = solo opacity-50, indistinguibile da "dim".
Sam (accessibilità): etichette 10px uppercase tracked ovunque; focus ring `ring-(--tone)/50` vicino a 3:1; disabled opacity-50 porta muted-foreground sotto 3:1.

## Minor Observations
1. Readout drag del knob a `-top-5` collide con la wavetable sopra (03-app-knob-drag.png).
2. Fader `aria-label` opzionale: A/D/S/R con nome di una lettera.
3. Master `md:w-64` orfano su riga propria (App.tsx:80).
4. `--color-text-dim` e `--color-warning` dichiarati ma inutilizzati.
5. `--color-master` = `--foreground`: il tono master sparisce.

## Questions to Consider
1. Tolti i pixel menta, qualcuno riconoscerebbe un synth? Oggi no: il colore fa il lavoro che dovrebbe fare la geometria.
2. Perché un plugin dentro una DAW ha header di pagina, h1 e gradiente hero?
3. La skeuomorfia regge a `size-knob-sm` (32px) o solo a `lg`?
