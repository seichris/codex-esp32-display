import AppKit

enum DeviceFilledIcon {
    // Shared 18-point geometry for the menu bar and the application artwork.
    // The 42 x 50.8 mm device is much squarer than the former tall pill.
    static func silhouette() -> NSBezierPath {
        let path = NSBezierPath()
        for y: CGFloat in [5, 10] {
            path.append(NSBezierPath(
                roundedRect: NSRect(x: 13.8, y: y, width: 1.2, height: 2),
                xRadius: 0.4, yRadius: 0.4
            ))
        }
        path.append(NSBezierPath(
            roundedRect: NSRect(x: 2.5, y: 2, width: 11.6, height: 14),
            xRadius: 2, yRadius: 2
        ))
        return path
    }

    static func makeTemplateImage() -> NSImage {
        let size = NSSize(width: 18, height: 18)
        let image = NSImage(size: size)
        image.lockFocus()
        NSColor.white.setFill()

        silhouette().fill()
        image.unlockFocus()
        image.isTemplate = true
        return image
    }

    static func drawApplicationIcon() {
        // Draw in a 1024-point canvas; the generator supplies the output scale.
        NSColor(calibratedRed: 0.08, green: 0.10, blue: 0.13, alpha: 1).setFill()
        NSBezierPath(roundedRect: NSRect(x: 100, y: 100, width: 824, height: 824),
                     xRadius: 184, yRadius: 184).fill()
        NSGraphicsContext.saveGraphicsState()
        let transform = NSAffineTransform()
        transform.translateX(by: 170, yBy: 170)
        transform.scale(by: 38)
        transform.concat()
        NSColor.white.setFill()
        silhouette().fill()
        NSGraphicsContext.restoreGraphicsState()
    }
}
