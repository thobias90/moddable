import { DeviceBehavior, DeviceContainer, DeviceScreen } from "DevicePane";
import { ControlsColumn } from "ControlsPane";
import { Client } from "websocket"
import Timer from "timer";

class SimulatorBehavior extends DeviceBehavior {
    constructor() {
        super();
        const startupClient = this.clientSetup.bind(this);
        Timer.set(startupClient, 1000);
    }

    clientSetup() {
        this.ws = new Client({host:"127.0.0.1"});
        this.ws.callback = function (message, value) {
            switch (message) {
                case Client.connect:
                    trace("socket connect\n");
                    break;

                case Client.handshake:
                    trace("websocket handshake success\n");
                    this.write(JSON.stringify({ count: 1, toggle: true }));
                    break;

                case Client.receive:
                    trace(`websocket message received: ${value}\n`);
                    value = JSON.parse(value);
                    value.count += 1;
                    value.toggle = !value.toggle;
                    this.write(JSON.stringify(value));
                    break;

                case Client.disconnect:
                    trace("websocket close\n");
                    break;
            }
        }

    }
}

export default {
    applicationName: "Simulator",
    title: "Simulator",
    ControlsTemplate: ControlsColumn.template($ => ({

    })),
    DeviceTemplate: DeviceContainer.template($ => ({
        Behavior: SimulatorBehavior,
        clip: true,
        contents: [
            DeviceScreen($, { width: 320, height: 240 })
        ]
    }))
}