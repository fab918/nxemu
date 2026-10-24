"""Decode a best-effort snapshot of NXEmu's opt-in Vulkan shared-memory ring."""
import argparse
import collections
import json
from pathlib import Path
import struct

NAMES = """Attach DisplayCreate DisplayDestroy TimelineCreate TimelineDestroy
TimelineWaitBegin TimelineWaitEnd SubmitBegin SubmitEnd QueueLockBegin QueueLockEnd
InterfaceWaitBegin InterfaceWaitEnd InterfaceSubmitBegin InterfaceSubmitEnd
AcquireBegin AcquireEnd PresentSubmitBegin PresentSubmitEnd PresentBegin PresentEnd
FrameWaitBegin FrameWaitEnd OwnPresentWaitBegin OwnPresentWaitEnd SwapchainCreate
RendererCreate RendererDestroy CompositeBegin CompositeEnd DeviceIdleBegin DeviceIdleEnd
ResourceWaitBegin ResourceWaitEnd""".split()
FIELDS = {
    "DisplayCreate": "object device", "TimelineCreate": "object device semaphore",
    "TimelineWaitBegin": "object tick known_gpu_tick current_tick semaphore",
    "TimelineWaitEnd": "object tick known_gpu_tick result",
    "SubmitBegin": "object tick timeline signal_binary wait_binary command upload_command queue",
    "SubmitEnd": "object tick result", "QueueLockBegin": "object tick_or_fresh",
    "QueueLockEnd": "object tick_or_fresh",
    "InterfaceWaitBegin": "object fence serial", "InterfaceWaitEnd": "object fence serial",
    "InterfaceSubmitBegin": "object fence command serial queue", "InterfaceSubmitEnd": "object result",
    "AcquireBegin": "object swapchain frame_index acquire_semaphore",
    "AcquireEnd": "object result frame_index image_index acquire_semaphore",
    "PresentSubmitBegin": "object fence acquire_semaphore render_ready render_finished command fresh queue",
    "PresentSubmitEnd": "object result",
    "PresentBegin": "object swapchain image_index frame_index render_finished queue",
    "PresentEnd": "object result", "FrameWaitBegin": "object frame fence render_ready",
    "FrameWaitEnd": "object frame fence", "OwnPresentWaitBegin": "object fence",
    "OwnPresentWaitEnd": "object fence", "SwapchainCreate": "object swapchain images mode width height",
    "RendererCreate": "object display scheduler", "CompositeBegin": "object layers",
    "ResourceWaitBegin": "object image_index tick current_tick", "ResourceWaitEnd": "object image_index tick",
}


def decode(data):
    if len(data) < 64:
        raise ValueError("Truncated trace header")
    magic, version, size, capacity, offset, frequency, next_seq, dropped = struct.unpack_from("<8sIIIIQQQ16x", data)
    if (magic, version, size, offset) != (b"NXVKTR01", 1, 128, 64) or not frequency or not capacity:
        raise ValueError("Unsupported or uninitialized trace")
    if len(data) != offset + size * capacity:
        raise ValueError("Truncated or invalid mapping size")
    events = []
    rejected = 0
    for slot in range(capacity):
        seq, writing, kind, timestamp, thread, _, *args = struct.unpack_from("<QIIQII12Q", data, offset + size * slot)
        if not seq:
            continue
        if writing or not max(1, next_seq - capacity + 1) <= seq <= next_seq or (seq - 1) % capacity != slot:
            rejected += 1
            continue
        name = NAMES[kind - 1] if 1 <= kind <= len(NAMES) else f"Unknown-{kind}"
        fields = FIELDS.get(name, "object").split()
        events.append({"seq": seq, "qpc": timestamp, "thread": thread, "event": name,
                       "args": dict(zip(fields, args))})
    events.sort(key=lambda item: item["seq"])
    origin = min((item["qpc"] for item in events), default=0)
    for item in events:
        item["ms"] = round((item["qpc"] - origin) * 1000 / frequency, 6)
    return {"version": version, "capacity": capacity, "next_sequence": next_seq,
            "writer_dropped": dropped, "snapshot_rejected": rejected,
            "qpc_frequency": frequency, "records": len(events)}, events


def summarize(metadata, events):
    pending = {}
    counts = collections.Counter()
    for item in events:
        name = item["event"]
        counts[name] += 1
        if name.endswith("Begin") or name == "QueueLockEnd":
            base = name.removesuffix("Begin").removesuffix("End")
            key = (item["thread"], base, item["args"].get("object", 0))
            if name.endswith("Begin"):
                pending[key] = item
            else:
                pending.pop(key, None)
        elif name.endswith("End"):
            key = (item["thread"], name.removesuffix("End"), item["args"].get("object", 0))
            pending.pop(key, None)
    return {"metadata": metadata, "counts": dict(counts),
            "unmatched_begins": list(pending.values()), "last_events": events[-40:],
            "note": "Unmatched begins are clues, not proof of a deadlock. A snapshot can overlap writes; inspect both captures."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    options = parser.parse_args()
    metadata, events = decode(options.trace.read_bytes())
    output = options.trace.with_suffix(".jsonl")
    with output.open("w", encoding="utf-8") as stream:
        for item in events:
            stream.write(json.dumps(item) + "\n")
    summary = options.trace.with_suffix(".summary.json")
    summary.write_text(json.dumps(summarize(metadata, events), indent=2), encoding="utf-8")
    print(json.dumps(metadata))
    print(f"Events: {output}\nSummary: {summary}")


if __name__ == "__main__":
    main()
