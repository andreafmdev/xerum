#include "standalone/XerumWindowMac.h"

#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>

/** Il guardiano dello stile chromeless: tiene il token dell'osservatore e lo cancella quando
    muore. Vive appeso alla NSWindow come associated object, quindi la sua vita e' esattamente
    quella della finestra e nessuno deve ricordarsi di disfare niente nel distruttore C++.
    Senza il -dealloc, il token resterebbe nel NSNotificationCenter per sempre: il centro tiene
    lui il riferimento forte, rilasciare il nostro non basta a disiscriversi.

    ARC e' spento (CLANG_ENABLE_OBJC_ARC non e' impostato dal progetto generato da CMake, e il
    default di Xcode e' NO), quindi qui si conta a mano: `retain`, `release` e `[super dealloc]`
    come nei .mm dei moduli JUCE. */
@interface XerumChromeKeeper : NSObject
@property (nonatomic, retain) id token;
@end

@implementation XerumChromeKeeper
- (void) dealloc
{
    if (_token != nil)
        [[NSNotificationCenter defaultCenter] removeObserver: _token];

    [_token release];
    [super dealloc];
}
@end

namespace
{
NSWindow* windowOf (void* nsViewHandle)
{
    if (nsViewHandle == nullptr)
        return nil;

    return [(__bridge NSView*) nsViewHandle window];
}

/** Chiave dell'associated object: l'indirizzo di se stessa, l'idioma ObjC per una chiave unica. */
const void* const kChromeKeeperKey = &kChromeKeeperKey;

void applyChromelessStyle (NSWindow* window)
{
    if (window == nil)
        return;

    window.titlebarAppearsTransparent = YES;
    window.titleVisibility = NSWindowTitleHidden;
    window.styleMask |= NSWindowStyleMaskFullSizeContentView;

    // La rete di sicurezza per le bande nere. Se la maschera e' stata rimessa DOPO che AppKit
    // aveva gia' calcolato il content rect, la content view resta alta 32 pt meno della finestra
    // e nessuno la ricalcola piu': misurato dopo un giro di full screen e un ridimensionamento —
    // finestra 648x490, maschera 0x800f (giusta), content view 648x458. Quei 32 pt scoperti sono
    // il background della finestra (0xff0e1016, quasi nero) in cima: la banda che l'utente vede
    // "stringendo la finestra". Con FullSizeContentView attivo il content rect coincide con il
    // frame, quindi qui basta confrontare le due altezze e rimettere la view a posto; scrivere il
    // frame della view del peer fa scattare redirectMovedOrResized e JUCE si riallinea da solo.
    NSView* content = window.contentView;

    if (content != nil)
    {
        NSRect wanted = [window contentRectForFrameRect: window.frame];
        wanted.origin = NSZeroPoint;

        if (! NSEqualRects (content.frame, wanted))
            content.frame = wanted;
    }
}
} // namespace

namespace xerum
{
void makeWindowChromeless (void* nsViewHandle)
{
    NSWindow* window = windowOf (nsViewHandle);

    if (window == nil)
        return;

    applyChromelessStyle (window);

    if (objc_getAssociatedObject (window, kChromeKeeperKey) != nil)
        return;

    // Perche' l'osservatore e non il solo resized(): uscendo dal full screen
    // NSViewComponentPeer::resetWindowPresentation() ASSEGNA lo styleMask dai soli flag di JUCE
    // (juce_NSViewComponentPeer_mac.mm:1613-1622), fra cui FullSizeContentView non c'e'. La tesi
    // precedente — "togliere il bit rimpicciolisce la content view, la view notifica, si passa da
    // resized() e lo rimettiamo" — e' stata verificata eseguendo l'app ed e' FALSA: subito dopo
    // l'uscita la maschera e' 0xf e la content view e' ancora alta quanto la finestra (900x680),
    // quindi nessuna notifica parte e la barra del titolo resta li' fino al primo trascinamento.
    //
    // Il notification center consegna ai suoi osservatori dopo il metodo del delegate (JUCE
    // registra la finestra come proprio delegate alla nascita del peer, molto prima di qui),
    // quindi si arriva quando resetWindowPresentation ha gia' fatto il danno. Coda nil = consegna
    // sincrona sul thread che posta, cioe' il main: cosi' non esiste nemmeno un frame disegnato
    // con la barra visibile.
    //
    // Il blocco non cattura `window`: usa note.object. Catturarla darebbe al centro di notifica un
    // riferimento forte alla finestra dentro un oggetto appeso alla finestra stessa.
    id token = [[NSNotificationCenter defaultCenter] addObserverForName: NSWindowDidExitFullScreenNotification
                                                                 object: window
                                                                  queue: nil
                                                             usingBlock: ^(NSNotification* note)
                                                             {
                                                                 applyChromelessStyle ((NSWindow*) note.object);
                                                             }];

    XerumChromeKeeper* keeper = [[XerumChromeKeeper alloc] init];
    keeper.token = token;
    objc_setAssociatedObject (window, kChromeKeeperKey, keeper, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    [keeper release]; // l'associated object l'ha ritenuto lui: qui si pareggia l'alloc
}

void installFullScreenMenuItem()
{
    NSMenu* mainMenu = [NSApp mainMenu];

    if (mainMenu == nil)
        return;

    // Idempotente: se un View c'e' gia' (ririchiamo, o un giorno un altro menu), non se ne fa un
    // secondo.
    for (NSMenuItem* item in mainMenu.itemArray)
        if ([item.title isEqualToString: @"View"])
            return;

    NSMenu* view = [[NSMenu alloc] initWithTitle: @"View"];

    // target nil = catena dei responder: toggleFullScreen: arriva alla finestra chiave, che e'
    // l'unica che abbiamo. Il titolo lo aggiorna AppKit da solo fra Enter ed Exit, perche'
    // riconosce l'azione.
    NSMenuItem* fullScreen = [[NSMenuItem alloc] initWithTitle: @"Enter Full Screen"
                                                       action: @selector (toggleFullScreen:)
                                                keyEquivalent: @"f"];
    fullScreen.keyEquivalentModifierMask = NSEventModifierFlagControl | NSEventModifierFlagCommand;
    [view addItem: fullScreen];
    [fullScreen release]; // -addItem: se l'e' ritenuto

    NSMenuItem* viewItem = [[NSMenuItem alloc] initWithTitle: @"View" action: nil keyEquivalent: @""];
    viewItem.submenu = view;
    [view release]; // -setSubmenu: se l'e' ritenuto

    // Dopo il menu dell'applicazione, che e' sempre il primo: e' il posto in cui macOS si aspetta
    // di trovare View, ed e' dove l'utente e le Impostazioni > Tastiera > Scorciatoie lo cercano.
    [mainMenu insertItem: viewItem atIndex: (mainMenu.numberOfItems > 0 ? 1 : 0)];
    [viewItem release];
}

double trafficLightWidth (void* nsViewHandle)
{
    NSWindow* window = windowOf (nsViewHandle);

    if (window == nil)
        return 0.0;

    NSButton* zoom = [window standardWindowButton: NSWindowZoomButton];

    if (zoom == nil)
        return 0.0;

    // Il bordo destro del bottone piu' a destra, piu' lo stesso margine che i tre hanno a
    // sinistra: e' lo spazio che la UI non deve occupare.
    NSButton* close = [window standardWindowButton: NSWindowCloseButton];
    const CGFloat leftMargin = close != nil ? NSMinX (close.frame) : 0.0;

    return (double) (NSMaxX (zoom.frame) + leftMargin);
}

bool beginNativeWindowDrag (void* nsViewHandle)
{
    NSWindow* window = windowOf (nsViewHandle);
    NSEvent* event = [NSApp currentEvent];

    if (window == nil || event == nil || event.type != NSEventTypeLeftMouseDown)
        return false;

    [window performWindowDragWithEvent: event];
    return true;
}

void toggleWindowZoom (void* nsViewHandle)
{
    [windowOf (nsViewHandle) zoom: nil];
}
} // namespace xerum
