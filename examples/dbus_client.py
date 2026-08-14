#!/usr/bin/env python3
"""Demonstrate input-proxy's public D-Bus interface.

This is a small educational example, not a production management client. It
requires ``dbus-next``::

    python3 -m pip install dbus-next

The dependency is needed only for this example; input-proxy itself does not
require Python or dbus-next.
"""

import asyncio
from contextlib import suppress
import sys

from dbus_next import BusType, DBusError
from dbus_next.aio import MessageBus


SERVICE_PREFIX = "net.controlforge.InputProxy1.Instance."
OBJECT_PATH = "/net/controlforge/InputProxy1/Instance"
INSTANCE_INTERFACE = "net.controlforge.InputProxy1.Instance"
PROPERTIES_INTERFACE = "org.freedesktop.DBus.Properties"
DBUS_SERVICE = "org.freedesktop.DBus"
DBUS_PATH = "/org/freedesktop/DBus"

PUBLIC_PROPERTIES = (
    "InstanceName",
    "Source",
    "Version",
    "PID",
    "Paused",
    "SourceAvailable",
    "ActivityWhileRunning",
    "ActivityWhilePaused",
)


def display_value(value):
    if isinstance(value, bool):
        return str(value).lower()
    return str(value)


async def read_command(prompt):
    """Read stdin without blocking D-Bus signal dispatch in the event loop."""
    loop = asyncio.get_running_loop()
    ready = loop.create_future()

    def stdin_ready():
        line = sys.stdin.readline()
        if not ready.done():
            ready.set_result(line.strip().lower() if line else "q")

    loop.add_reader(sys.stdin.fileno(), stdin_ready)
    try:
        print(prompt, end="", flush=True)
        return await ready
    finally:
        loop.remove_reader(sys.stdin.fileno())


async def choose_instance(dbus_interface):
    names = await dbus_interface.call_list_names()
    instances = sorted(
        name[len(SERVICE_PREFIX) :]
        for name in names
        if name.startswith(SERVICE_PREFIX)
    )

    if not instances:
        print("No running input-proxy instances were found.")
        return None

    print("Running input-proxy instances:")
    for number, name in enumerate(instances, start=1):
        print(f"  {number}. {name}")

    while True:
        selection = await read_command("Select an instance: ")
        try:
            index = int(selection) - 1
        except ValueError:
            index = -1
        if 0 <= index < len(instances):
            return instances[index]
        print(f"Please enter a number from 1 to {len(instances)}.")


async def run_client(instance_name):
    bus = await MessageBus(bus_type=BusType.SYSTEM).connect()
    try:
        dbus_introspection = await bus.introspect(DBUS_SERVICE, DBUS_PATH)
        dbus_object = bus.get_proxy_object(
            DBUS_SERVICE, DBUS_PATH, dbus_introspection
        )
        dbus_interface = dbus_object.get_interface(DBUS_SERVICE)

        if instance_name is None:
            instance_name = await choose_instance(dbus_interface)
            if instance_name is None:
                return

        service_name = SERVICE_PREFIX + instance_name
        introspection = await bus.introspect(service_name, OBJECT_PATH)
        instance_object = bus.get_proxy_object(
            service_name, OBJECT_PATH, introspection
        )
        instance_interface = instance_object.get_interface(INSTANCE_INTERFACE)
        properties_interface = instance_object.get_interface(PROPERTIES_INTERFACE)

        variants = await properties_interface.call_get_all(INSTANCE_INTERFACE)
        current = {name: variants[name].value for name in PUBLIC_PROPERTIES}

        print(f"\nConnected to {instance_name}\n")
        for name in PUBLIC_PROPERTIES:
            print(f"{name}: {display_value(current[name])}")

        unavailable = asyncio.Event()

        def properties_changed(interface_name, changed, invalidated):
            if interface_name != INSTANCE_INTERFACE:
                return
            for name, variant in changed.items():
                if name not in PUBLIC_PROPERTIES:
                    continue
                old_value = current.get(name)
                new_value = variant.value
                print(
                    f"\n{name}: {display_value(old_value)} -> "
                    f"{display_value(new_value)}"
                )
                current[name] = new_value
            if changed:
                print("command (p/r/q): ", end="", flush=True)

        properties_interface.on_properties_changed(properties_changed)

        async def watch_service():
            while await dbus_interface.call_name_has_owner(service_name):
                await asyncio.sleep(0.5)
            unavailable.set()

        watcher = asyncio.create_task(watch_service())
        print("\nCommands: p = pause, r = resume, q = quit")
        try:
            while True:
                command_task = asyncio.create_task(read_command("command (p/r/q): "))
                unavailable_task = asyncio.create_task(unavailable.wait())
                done, pending = await asyncio.wait(
                    (command_task, unavailable_task),
                    return_when=asyncio.FIRST_COMPLETED,
                )
                for task in pending:
                    task.cancel()
                await asyncio.gather(*pending, return_exceptions=True)

                if unavailable_task in done:
                    print(f"\nInstance {instance_name} became unavailable.")
                    return

                command = command_task.result()
                if command == "q":
                    return
                if command == "p":
                    await instance_interface.call_pause()
                elif command == "r":
                    await instance_interface.call_resume()
                else:
                    print("Enter p to pause, r to resume, or q to quit.")
        finally:
            watcher.cancel()
            with suppress(asyncio.CancelledError):
                await watcher
            properties_interface.off_properties_changed(properties_changed)
    finally:
        bus.disconnect()


def main():
    if len(sys.argv) > 2:
        print(f"Usage: {sys.argv[0]} [InstanceName]", file=sys.stderr)
        return 2

    instance_name = sys.argv[1] if len(sys.argv) == 2 else None
    try:
        asyncio.run(run_client(instance_name))
    except (DBusError, ConnectionError, OSError) as error:
        message = getattr(error, "text", None) or str(error)
        print(f"D-Bus error: {message}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
