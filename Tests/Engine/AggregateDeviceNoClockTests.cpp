// JUCE's CoreAudio device type must survive an aggregate audio device whose clock properties cannot be read
// (cmake/patches/juce-aggregate-null-clock.patch). Stock JUCE called CFStringGetLength on the null string that
// AggregateAudioDevice::getClockingDeviceUid() gets for such a device, so AudioIODeviceType::createDevice crashed
// with SIGSEGV at address 0. AudioEngine::countDeviceInputs() calls createDevice for every device the user sees,
// so another process creating and destroying a private aggregate device was enough to take the app down.
//
// The test builds that state with the real HAL and no audio hardware being opened: a private aggregate with one
// sub-device is created and scanned (an aggregate with no channels is not listed at all), then destroyed. For a
// short window after the destroy the HAL still answers the class query but fails every property query, which is
// exactly what a device that another process just removed looks like to JUCE. The test polls for that window and
// calls createDevice inside it. Unique name and UID per process, because test shards run in parallel.

#include <juce_core/juce_core.h>

#if JUCE_MAC

#include <chrono>
#include <gtest/gtest.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <memory>
#include <type_traits>

#include <CoreAudio/AudioHardware.h>
#include <CoreFoundation/CoreFoundation.h>
#include <unistd.h>

namespace {

template <typename T>
using CfOwner = std::unique_ptr<std::remove_pointer_t<T>, decltype(&CFRelease)>;

template <typename T>
CfOwner<T> own(T ref) {
    return CfOwner<T>(ref, &CFRelease);
}

template <typename T>
T readProperty(AudioObjectID object, AudioObjectPropertySelector selector, OSStatus& status) {
    const AudioObjectPropertyAddress address{selector, kAudioObjectPropertyScopeGlobal,
                                             kAudioObjectPropertyElementMain};
    T value{};
    UInt32 size = sizeof(value);
    status = AudioObjectGetPropertyData(object, &address, 0, nullptr, &size, &value);
    return value;
}

AudioObjectID defaultOutputDevice() {
    OSStatus status = 0;
    return readProperty<AudioObjectID>(kAudioObjectSystemObject, kAudioHardwarePropertyDefaultOutputDevice, status);
}

// A private aggregate that exists for the life of this object and is destroyed on every exit path.
class ScopedPrivateAggregate {
public:
    ScopedPrivateAggregate(const juce::String& name, const juce::String& uid, CFStringRef subDeviceUid) {
        const auto cf = [](const juce::String& s) { return own(s.toCFString()); };
        const auto nameCf = cf(name);
        const auto uidCf = cf(uid);

        const auto subDevice = own<CFMutableDictionaryRef>(
            CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
        CFDictionarySetValue(subDevice.get(), CFSTR(kAudioSubDeviceUIDKey), subDeviceUid);
        const void* subDevices[] = {subDevice.get()};
        const auto subDeviceList = own<CFArrayRef>(CFArrayCreate(nullptr, subDevices, 1, &kCFTypeArrayCallBacks));

        const int isPrivate = 1;
        const auto privateFlag = own<CFNumberRef>(CFNumberCreate(nullptr, kCFNumberIntType, &isPrivate));

        // No clock-device key and no main-sub-device key: the aggregate has neither.
        const auto description = own<CFMutableDictionaryRef>(
            CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
        CFDictionarySetValue(description.get(), CFSTR(kAudioAggregateDeviceNameKey), nameCf.get());
        CFDictionarySetValue(description.get(), CFSTR(kAudioAggregateDeviceUIDKey), uidCf.get());
        CFDictionarySetValue(description.get(), CFSTR(kAudioAggregateDeviceIsPrivateKey), privateFlag.get());
        CFDictionarySetValue(description.get(), CFSTR(kAudioAggregateDeviceSubDeviceListKey), subDeviceList.get());

        status_ = AudioHardwareCreateAggregateDevice(description.get(), &id_);
    }

    ~ScopedPrivateAggregate() { destroy(); }
    ScopedPrivateAggregate(const ScopedPrivateAggregate&) = delete;
    ScopedPrivateAggregate& operator=(const ScopedPrivateAggregate&) = delete;

    bool created() const { return status_ == noErr && id_ != kAudioObjectUnknown; }
    AudioObjectID id() const { return id_; }

    void destroy() {
        if (created() && !destroyed_)
            AudioHardwareDestroyAggregateDevice(id_);
        destroyed_ = true;
    }

private:
    AudioObjectID id_ = kAudioObjectUnknown;
    OSStatus status_ = -1;
    bool destroyed_ = false;
};

// True while the HAL still reports `device` as an aggregate but can no longer read its clock device.
bool isHalledDeadAggregate(AudioObjectID device) {
    OSStatus status = 0;
    const auto deviceClass = readProperty<AudioClassID>(device, kAudioObjectPropertyClass, status);
    if (status != noErr || deviceClass != kAudioAggregateDeviceClassID)
        return false;
    const auto clock = readProperty<CFStringRef>(device, kAudioAggregateDevicePropertyClockDevice, status);
    return status != noErr || clock == nullptr;
}

TEST(AggregateDeviceNoClockTest, CreateDeviceOnAnAggregateWhoseClockCannotBeReadDoesNotCrash) {
    const auto output = defaultOutputDevice();
    if (output == kAudioObjectUnknown)
        GTEST_SKIP() << "no default audio output device on this machine";

    OSStatus status = 0;
    const auto outputUid = readProperty<CFStringRef>(output, kAudioDevicePropertyDeviceUID, status);
    if (status != noErr || outputUid == nullptr)
        GTEST_SKIP() << "the default output device has no UID";
    const auto outputUidOwner = own<CFStringRef>(outputUid);

    std::unique_ptr<juce::AudioIODeviceType> type(juce::AudioIODeviceType::createAudioIODeviceType_CoreAudio());
    ASSERT_NE(type, nullptr);

    int windowsEntered = 0;
    constexpr int kAttempts = 40;
    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        const auto tag = juce::String(getpid()) + "-" + juce::String(attempt);
        const auto name = "AgentSynthNoClockTest-" + tag;
        ScopedPrivateAggregate aggregate(name, "com.agentsynth.tests.noclock." + tag, outputUid);
        if (!aggregate.created())
            GTEST_SKIP() << "this machine does not allow creating an aggregate audio device";

        type->scanForDevices();
        if (!type->getDeviceNames(false).contains(name))
            GTEST_SKIP() << "the CoreAudio device type did not list the test aggregate";

        aggregate.destroy();

        // Poll for the window in which the aggregate answers a class query but fails property reads, then probe
        // inside it. Not entering the window in one attempt is not a failure; the next attempt tries again.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        while (std::chrono::steady_clock::now() < deadline) {
            OSStatus classStatus = 0;
            readProperty<AudioClassID>(aggregate.id(), kAudioObjectPropertyClass, classStatus);
            if (classStatus != noErr)
                break; // fully gone: the window has passed
            if (isHalledDeadAggregate(aggregate.id())) {
                ++windowsEntered;
                const std::unique_ptr<juce::AudioIODevice> device(type->createDevice(name, {}));
                break;
            }
        }
    }

    // Not entering the window at all would make the loop above prove nothing.
    EXPECT_GT(windowsEntered, 0) << "never caught the aggregate between its class and its properties going away";
}

} // namespace

#endif // JUCE_MAC
