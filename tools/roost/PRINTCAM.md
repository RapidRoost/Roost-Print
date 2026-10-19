# PrintCam in Roost Print

PrintCam (Rapid Roost's camera relay, in the website repo under `printcam-relay/`) does the real
work: it watches the printers, matches each print to an order, tells rapidroost.net how it's going,
sends photos and timelapses, and asks The Farm before emailing a customer that their print started.

Roost Print's job is to make PrintCam never miss. It does four things.

## 1. Every print it sends is named for its order

PrintCam knows whose print it is only from the file name on the printer: `RR` + the order number
(`RR1042_bracket.gcode`, two orders: `RR1042_RR1043_...`). So:

- **Send print** / **Send to multi-device** first works out the order: from the project or object
  names if they already say `RR1042`, else it asks ("Which order is this print for?"). It remembers
  the answer for the same plate. **Shop print, no order** sends without one. **Cancel** stops the send.
- The send window's default file name then starts with the order number(s), and is cleaned to
  letters, digits, spaces, `_`, `-` and `.` (apostrophes, accents, `#` and `;` have broken Creality
  prints). The window shows that name, uploads it and starts the print with it, so they always match.
- The rule is the same as PrintCam's `OrdersInFileName` (orders.go). `RoostPrintCam::orders_in_name`
  passes all of PrintCam's own test cases. **If the rule changes in orders.go, change it here too.**

Code: `orders_in_name`, `order_file_name`, `orders_for_plate` in `src/slic3r/GUI/RoostPrintCam.cpp`;
used in `Plater.cpp` (before the send window opens) and `print_manage/App/SendToPrinter.cpp`
(`upload_gcode__name` and the 3MF project name).

## 2. PrintCam is always running

Roost Print checks PrintCam's page (`http://127.0.0.1:8788/api/state`) every 8 seconds, in the
background. If PrintCam doesn't answer and is installed, Roost Print starts it (hidden, at most once
a minute). PrintCam is found at: the path saved from "Find PrintCam.exe", the one Windows starts at
sign-in, `C:\PrintCam\PrintCam.exe`, or next to Roost Print.

## 3. Nothing waits unseen

While customers are waiting for a yes/no on their "your print started" email, the tab says
**PrintCam (2)** and a note in the 3D view says which orders, with **Open PrintCam**. The yes/no
itself stays in PrintCam (its page, and its Windows pop-up), so there is one place that decides.

## 4. Problems show up while you work

A warning in the 3D view (only after two checks in a row, so a blip doesn't nag) when:

- PrintCam isn't running (and Roost Print is starting it), or isn't on this computer;
- PrintCam can't reach rapidroost.net (`sync.ok` false, with its error);
- a printer is printing a file with no order number (`printers[].orders` empty while busy);
- an order number isn't on the website, which is usually a typo (`sync.unknown`).

## What Roost Print reads from PrintCam

Only `GET /api/state` (local, no secret needed), and only these fields:
`sync.last`, `sync.ok`, `sync.error`, `sync.unknown[]`, `emails[].order` and `emails[].state`
(`"ask"`), `printers[].name`, `printers[].status.state`, `printers[].orders[]`.
**If PrintCam's /api/state changes shape, change `parse_state` in RoostPrintCam.cpp.**

Roost Print never holds PrintCam's secret or settings, and sends nothing to the website itself.

## Not done yet (decisions for Dylan)

- **Text messages.** PrintCam only emails. Customers who gave only a phone number get nothing.
  Texting needs an SMS service (for example Twilio, a few cents per text) or Shopify's own
  marketing texts; that's a cost and an outside service, so it's his call.
- **K2 Plus cameras.** Newer K2 firmware closes camera port 8000; those printers show "no camera"
  in PrintCam (progress still works). The firmware update in October 2026 may have done this.
