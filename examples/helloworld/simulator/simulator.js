import { DeviceBehavior, DeviceContainer, DeviceScreen } from "DevicePane";
import { ControlsColumn } from "ControlsPane";

class SimulatorBehavior extends DeviceBehavior {
    constructor() {
        super();
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
            DeviceScreen($, {width: 320, height: 240})
        ]
    }))
}