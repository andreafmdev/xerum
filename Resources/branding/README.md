# Xerum — asset del marchio

Ricostruzione vettoriale del riferimento fornito: proporzioni, colori e lettering sono ridisegnati, non estratti da un originale vettoriale. Nessun font o bitmap incorporato negli SVG in `svg/`.

## File da usare

| Utilizzo | Asset |
| --- | --- |
| Logo completo su fondo scuro | `svg/xerum-logo-dark.svg` |
| Logo completo senza sfondo | `svg/xerum-logo-transparent.svg` |
| Simbolo con vetro e neon | `svg/xerum-mark-dark.svg` o `xerum-mark-transparent.svg` |
| Simbolo senza filtri, dimensioni piccole | `svg/xerum-mark-simple.svg` o `xerum-mark-simple-dark.svg` |
| Monocromatico bianco / scuro | `svg/xerum-mark-mono-light.svg` / `xerum-mark-mono-dark.svg` |
| Icona macOS | `icons/xerum.icns` |
| Icona Windows | `icons/xerum.ico` |
| Icone PNG | `icons/xerum-{16,32,48,64,128,256,512,1024}.png` |

`preview.png` mostra tutte le varianti. In `png/` sono disponibili anche le esportazioni a 1254 × 1254, comprese quelle trasparenti. `icons/xerum.iconset/` contiene le rappresentazioni macOS standard e Retina.

I loghi trasparenti con lettering chiaro sono pensati per superfici scure. Per sfondi chiari usare il simbolo monocromatico scuro. Conservare il rapporto 1:1 e lo spazio di rispetto incluso. Per piccole icone usare la sola X, non il logo con sottotitolo.

I master fedeli utilizzano gradienti, clipping e filtri SVG per i bagliori. Nei renderer che non supportano questi effetti usare i PNG oppure gli SVG `simple` / `mono`. Le icone PNG fino a 48 px e le rappresentazioni macOS con dimensione logica fino a 32 pt usano il disegno semplificato. L'ICO contiene frame da 16, 32, 48, 64, 128 e 256 px.

## Rigenerazione

Richiede Node.js e `@resvg/resvg-js` 2.6.2, installabile separatamente dalle dipendenze dell'app. Da macOS, con `iconutil` disponibile:

```sh
node Resources/branding/build.mjs /percorso/node_modules/@resvg/resvg-js
zip -r -X Resources/xerum-branding.zip Resources/branding
```

`build.mjs` contiene la geometria sorgente, genera SVG e raster, compone l'ICO e invoca `iconutil` per l'ICNS. Non modifica build o interfaccia dell'app. Il foglio di anteprima usa testo di servizio; tutti i testi del marchio nei master SVG sono tracciati.
