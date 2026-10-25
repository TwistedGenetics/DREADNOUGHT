// SPDX-License-Identifier: GPL-3.0-only
// DREADNOUGHT for iPad. Copyright (C) 2026 Jamie (TwistedGenetics).

import UIKit

@main
final class AppDelegate: UIResponder, UIApplicationDelegate {
    var window: UIWindow?

    func application(_ application: UIApplication,
                     didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]?) -> Bool {
        _ = FilesBridge.projects                         // (the Projects folder is there in Files from the start)
        let w = UIWindow(frame: UIScreen.main.bounds)
        w.rootViewController = EditorViewController()
        w.makeKeyAndVisible()
        window = w
        return true
    }
}
