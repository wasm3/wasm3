//
//  ViewController.swift
//  wasm3
//
//  Created by Volodymyr Shymanskyy on 1/10/20.
//  Copyright © 2020 wasm3. All rights reserved.
//

import UIKit

var gLog: UITextView!

class ViewController: UIViewController {

    // MARK: Properties
    @IBOutlet var log: UITextView!

    override func viewDidLoad() {
        super.viewDidLoad()

        gLog = log

        redirect_output({
            if let ptr = $0 {
                let data = Data(bytes: ptr, count: Int($1))
                if let str = String(data: data, encoding: String.Encoding.utf8) {
                    DispatchQueue.main.async {
                        gLog.text += str
                    }
                }
            }
        })

        run_app()
    }

    override func didReceiveMemoryWarning() {
        super.didReceiveMemoryWarning()
    }
}

