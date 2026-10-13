namespace OpenGlance;

/// A Bluetooth LE advertisement seen while searching.
public sealed record Sighting(ulong Address, string? Name, int Signal, IReadOnlyList<string> Services);

/// The radio, kept small so the rest of the app is platform-neutral.
public interface IClockRadio
{
    void StartScan(Action<Sighting> found);
    void StopScan();
    /// Connects (pairing first if needed, asking for the PIN shown on the clock) and opens the Glance service.
    Task<IClockLink> ConnectAsync(ulong address, Func<Task<string?>> askPin, CancellationToken cancel);
}

public interface IClockLink : IDisposable
{
    string Name { get; }
    int MaxWrite { get; }
    Task<byte[]> ReadControlAsync();
    Task WriteControlAsync(byte[] data);
    Task<byte[]?> ReadStateAsync();
    Task<int?> ReadBatteryAsync();
    Task<string?> ReadFirmwareAsync();
    event Action<byte[]>? StateChanged;
    event Action? Disconnected;
}

public static class Radio
{
#if WINDOWS
    public static IClockRadio? Create() => new WinRadio();
#else
    public static IClockRadio? Create() => null;
#endif
}

#if WINDOWS
sealed class WinRadio : IClockRadio
{
    Windows.Devices.Bluetooth.Advertisement.BluetoothLEAdvertisementWatcher? watcher;

    public void StartScan(Action<Sighting> found)
    {
        StopScan();
        watcher = new() { ScanningMode = Windows.Devices.Bluetooth.Advertisement.BluetoothLEScanningMode.Active };
        watcher.Received += (_, a) =>
        {
            var services = a.Advertisement.ServiceUuids.Select(g => g.ToString()).ToList();
            found(new Sighting(a.BluetoothAddress, string.IsNullOrEmpty(a.Advertisement.LocalName) ? null : a.Advertisement.LocalName, a.RawSignalStrengthInDBm, services));
        };
        watcher.Start();
    }

    public void StopScan()
    {
        try { watcher?.Stop(); } catch { }
        watcher = null;
    }

    public async Task<IClockLink> ConnectAsync(ulong address, Func<Task<string?>> askPin, CancellationToken cancel)
    {
        var device = await Windows.Devices.Bluetooth.BluetoothLEDevice.FromBluetoothAddressAsync(address).AsTask(cancel)
            ?? throw new InvalidOperationException("Windows could not find the clock. Short-press the clock's button to activate pairing mode, keep it nearby, then try again.");
        try
        {
            var pairing = device.DeviceInformation.Pairing;
            if (!pairing.IsPaired)
            {
                var custom = pairing.Custom;
                async void OnPairing(Windows.Devices.Enumeration.DeviceInformationCustomPairing sender, Windows.Devices.Enumeration.DevicePairingRequestedEventArgs e)
                {
                    var deferral = e.GetDeferral();
                    try
                    {
                        if (e.PairingKind == Windows.Devices.Enumeration.DevicePairingKinds.ProvidePin)
                        {
                            var pin = await askPin();
                            if (!string.IsNullOrWhiteSpace(pin)) e.Accept(pin.Trim());
                        }
                        else e.Accept();   // confirm-only, or a PIN shown on the clock that matches
                    }
                    finally { deferral.Complete(); }
                }
                custom.PairingRequested += OnPairing;
                try
                {
                    var kinds = Windows.Devices.Enumeration.DevicePairingKinds.ProvidePin | Windows.Devices.Enumeration.DevicePairingKinds.ConfirmOnly
                        | Windows.Devices.Enumeration.DevicePairingKinds.ConfirmPinMatch | Windows.Devices.Enumeration.DevicePairingKinds.DisplayPin;
                    var result = await custom.PairAsync(kinds, Windows.Devices.Enumeration.DevicePairingProtectionLevel.EncryptionAndAuthentication).AsTask(cancel);
                    if (result.Status is not (Windows.Devices.Enumeration.DevicePairingResultStatus.Paired or Windows.Devices.Enumeration.DevicePairingResultStatus.AlreadyPaired))
                        throw new InvalidOperationException(PairingProblem(result.Status));
                }
                finally { custom.PairingRequested -= OnPairing; }
            }
            var session = await Windows.Devices.Bluetooth.GenericAttributeProfile.GattSession.FromDeviceIdAsync(device.BluetoothDeviceId).AsTask(cancel);
            session.MaintainConnection = true;
            var link = new WinLink(device, session);
            await link.OpenAsync(cancel);
            return link;
        }
        catch { device.Dispose(); throw; }
    }

    static string PairingProblem(Windows.Devices.Enumeration.DevicePairingResultStatus status) => status switch
    {
        Windows.Devices.Enumeration.DevicePairingResultStatus.AuthenticationFailure or Windows.Devices.Enumeration.DevicePairingResultStatus.Failed
            => "Pairing failed. Check the PIN shown on the clock and try again.",
        Windows.Devices.Enumeration.DevicePairingResultStatus.PairingCanceled => "Pairing was cancelled.",
        Windows.Devices.Enumeration.DevicePairingResultStatus.AuthenticationTimeout or Windows.Devices.Enumeration.DevicePairingResultStatus.ConnectionRejected
            => "The clock did not accept pairing. Short-press its button to activate pairing mode, then try again.",
        _ => $"Pairing did not complete ({status}). Short-press the clock's button to activate pairing mode, then try again.",
    };
}

sealed class WinLink : IClockLink
{
    readonly Windows.Devices.Bluetooth.BluetoothLEDevice device;
    readonly Windows.Devices.Bluetooth.GenericAttributeProfile.GattSession session;
    readonly List<IDisposable> services = new();
    Windows.Devices.Bluetooth.GenericAttributeProfile.GattCharacteristic? control, state, battery, firmware;
    public event Action<byte[]>? StateChanged;
    public event Action? Disconnected;
    public string Name => string.IsNullOrEmpty(device.Name) ? "Glance Clock" : device.Name;
    public int MaxWrite => Math.Max(20, session.MaxPduSize - 3);

    public WinLink(Windows.Devices.Bluetooth.BluetoothLEDevice device, Windows.Devices.Bluetooth.GenericAttributeProfile.GattSession session)
    {
        this.device = device; this.session = session;
        device.ConnectionStatusChanged += (d, _) =>
        {
            if (d.ConnectionStatus == Windows.Devices.Bluetooth.BluetoothConnectionStatus.Disconnected) Disconnected?.Invoke();
        };
    }

    async Task<Windows.Devices.Bluetooth.GenericAttributeProfile.GattCharacteristic?> Find(Guid service, Guid characteristic, CancellationToken cancel)
    {
        var s = await device.GetGattServicesForUuidAsync(service, Windows.Devices.Bluetooth.BluetoothCacheMode.Uncached).AsTask(cancel);
        if (s.Status != Windows.Devices.Bluetooth.GenericAttributeProfile.GattCommunicationStatus.Success || s.Services.Count == 0) return null;
        var svc = s.Services[0];
        services.Add(svc);
        var c = await svc.GetCharacteristicsForUuidAsync(characteristic, Windows.Devices.Bluetooth.BluetoothCacheMode.Uncached).AsTask(cancel);
        return c.Status == Windows.Devices.Bluetooth.GenericAttributeProfile.GattCommunicationStatus.Success && c.Characteristics.Count > 0 ? c.Characteristics[0] : null;
    }

    public async Task OpenAsync(CancellationToken cancel)
    {
        var glance = Guid.Parse(GlanceProtocol.GlanceCommands.ServiceUuid);
        control = await Find(glance, Guid.Parse(GlanceProtocol.GlanceCommands.ControlUuid), cancel)
            ?? throw new InvalidOperationException("This device does not expose the Glance Clock control characteristic.");
        var props = control.CharacteristicProperties;
        if (!props.HasFlag(Windows.Devices.Bluetooth.GenericAttributeProfile.GattCharacteristicProperties.Read) || !props.HasFlag(Windows.Devices.Bluetooth.GenericAttributeProfile.GattCharacteristicProperties.Write))
            throw new InvalidOperationException("This clock's control characteristic does not support the expected read/write access.");
        // The hand-state characteristic lives in the same service.
        if (services.LastOrDefault() is Windows.Devices.Bluetooth.GenericAttributeProfile.GattDeviceService gs)
        {
            var st = await gs.GetCharacteristicsForUuidAsync(Guid.Parse(GlanceProtocol.GlanceCommands.StateUuid), Windows.Devices.Bluetooth.BluetoothCacheMode.Uncached).AsTask(cancel);
            if (st.Status == Windows.Devices.Bluetooth.GenericAttributeProfile.GattCommunicationStatus.Success && st.Characteristics.Count > 0) state = st.Characteristics[0];
        }
        battery = await Find(Windows.Devices.Bluetooth.GenericAttributeProfile.GattServiceUuids.Battery, Windows.Devices.Bluetooth.GenericAttributeProfile.GattCharacteristicUuids.BatteryLevel, cancel);
        firmware = await Find(Windows.Devices.Bluetooth.GenericAttributeProfile.GattServiceUuids.DeviceInformation, Windows.Devices.Bluetooth.GenericAttributeProfile.GattCharacteristicUuids.FirmwareRevisionString, cancel);
        if (state is not null && state.CharacteristicProperties.HasFlag(Windows.Devices.Bluetooth.GenericAttributeProfile.GattCharacteristicProperties.Notify))
        {
            state.ValueChanged += (_, a) => StateChanged?.Invoke(ToBytes(a.CharacteristicValue));
            try { await state.WriteClientCharacteristicConfigurationDescriptorAsync(Windows.Devices.Bluetooth.GenericAttributeProfile.GattClientCharacteristicConfigurationDescriptorValue.Notify).AsTask(cancel); } catch { }
        }
    }

    static byte[] ToBytes(Windows.Storage.Streams.IBuffer buffer)
    {
        var bytes = new byte[buffer.Length];
        using var reader = Windows.Storage.Streams.DataReader.FromBuffer(buffer);
        reader.ReadBytes(bytes);
        return bytes;
    }

    static async Task<byte[]> Read(Windows.Devices.Bluetooth.GenericAttributeProfile.GattCharacteristic c)
    {
        var r = await c.ReadValueAsync(Windows.Devices.Bluetooth.BluetoothCacheMode.Uncached);
        if (r.Status != Windows.Devices.Bluetooth.GenericAttributeProfile.GattCommunicationStatus.Success)
            throw new InvalidOperationException(r.Status == Windows.Devices.Bluetooth.GenericAttributeProfile.GattCommunicationStatus.AccessDenied
                ? "The clock refused access. Remove it in Windows Settings → Bluetooth & devices, short-press its button, then pair again."
                : $"Bluetooth read failed ({r.Status}). Keep the clock nearby and try again.");
        return ToBytes(r.Value);
    }

    public Task<byte[]> ReadControlAsync() => Read(control!);

    public async Task WriteControlAsync(byte[] data)
    {
        using var writer = new Windows.Storage.Streams.DataWriter();
        writer.WriteBytes(data);
        var r = await control!.WriteValueWithResultAsync(writer.DetachBuffer(), Windows.Devices.Bluetooth.GenericAttributeProfile.GattWriteOption.WriteWithResponse);
        if (r.Status != Windows.Devices.Bluetooth.GenericAttributeProfile.GattCommunicationStatus.Success)
            throw new InvalidOperationException($"The clock did not accept the command ({r.Status}{(r.ProtocolError is byte e ? $", error {e}" : "")}).");
    }

    public async Task<byte[]?> ReadStateAsync() => state is null ? null : await Read(state);

    public async Task<int?> ReadBatteryAsync()
    {
        if (battery is null) return null;
        var b = await Read(battery);
        return b.Length > 0 && b[0] <= 100 ? b[0] : null;
    }

    public async Task<string?> ReadFirmwareAsync()
    {
        if (firmware is null) return null;
        var text = System.Text.Encoding.UTF8.GetString(await Read(firmware)).Trim('\0', ' ', '\r', '\n');
        return text.Length == 0 ? null : text;
    }

    public void Dispose()
    {
        try { session.MaintainConnection = false; } catch { }
        foreach (var s in services) { try { s.Dispose(); } catch { } }
        try { session.Dispose(); } catch { }
        try { device.Dispose(); } catch { }
    }
}
#endif
