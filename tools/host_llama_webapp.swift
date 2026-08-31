import Cocoa
import WebKit

/// Ventana WebKit (motor de Safari) sin pestañas ni barra de URL.
final class App: NSObject, NSApplicationDelegate, WKUIDelegate {
    let startURL: URL
    var window: NSWindow?

    init(url: URL) {
        self.startURL = url
        super.init()
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        let rect = NSRect(x: 0, y: 0, width: 1180, height: 780)
        let style: NSWindow.StyleMask = [
            .titled, .closable, .miniaturizable, .resizable, .fullSizeContentView,
        ]
        let win = NSWindow(contentRect: rect, styleMask: style, backing: .buffered, defer: false)
        win.title = "tuide host"
        win.titleVisibility = .hidden
        win.titlebarAppearsTransparent = true
        win.backgroundColor = NSColor(red: 16 / 255, green: 17 / 255, blue: 20 / 255, alpha: 1)
        win.isMovableByWindowBackground = true
        win.minSize = NSSize(width: 720, height: 480)
        win.appearance = NSAppearance(named: .darkAqua)

        let wv = WKWebView(frame: win.contentView?.bounds ?? rect)
        wv.autoresizingMask = [.width, .height]
        wv.customUserAgent = "tuide-host-webapp"
        wv.uiDelegate = self
        win.contentView = wv
        wv.load(URLRequest(url: startURL))

        win.center()
        win.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        window = win
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        true
    }

    func webView(
        _ webView: WKWebView,
        runJavaScriptAlertPanelWithMessage message: String,
        initiatedByFrame frame: WKFrameInfo,
        completionHandler: @escaping () -> Void
    ) {
        let alert = NSAlert()
        alert.messageText = message
        alert.addButton(withTitle: "OK")
        alert.runModal()
        completionHandler()
    }

    func webView(
        _ webView: WKWebView,
        runJavaScriptConfirmPanelWithMessage message: String,
        initiatedByFrame frame: WKFrameInfo,
        completionHandler: @escaping (Bool) -> Void
    ) {
        let alert = NSAlert()
        alert.messageText = message
        alert.addButton(withTitle: "OK")
        alert.addButton(withTitle: "Cancelar")
        completionHandler(alert.runModal() == .alertFirstButtonReturn)
    }

    func webView(
        _ webView: WKWebView,
        runJavaScriptTextInputPanelWithPrompt prompt: String,
        defaultText: String?,
        initiatedByFrame frame: WKFrameInfo,
        completionHandler: @escaping (String?) -> Void
    ) {
        let alert = NSAlert()
        alert.messageText = prompt
        alert.addButton(withTitle: "OK")
        alert.addButton(withTitle: "Cancelar")
        let field = NSTextField(frame: NSRect(x: 0, y: 0, width: 240, height: 24))
        field.stringValue = defaultText ?? ""
        alert.accessoryView = field
        if alert.runModal() == .alertFirstButtonReturn {
            completionHandler(field.stringValue)
        } else {
            completionHandler(nil)
        }
    }
}

guard CommandLine.arguments.count >= 2,
      let url = URL(string: CommandLine.arguments[1]),
      let scheme = url.scheme?.lowercased(),
      scheme == "http" || scheme == "https"
else {
    fputs("uso: tuide-host-webapp <http-url>\n", stderr)
    exit(2)
}

let app = NSApplication.shared
let delegate = App(url: url)
app.delegate = delegate
app.setActivationPolicy(.regular)
app.run()
