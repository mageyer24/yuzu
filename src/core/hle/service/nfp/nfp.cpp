// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <atomic>

#include "common/logging/log.h"
#include "core/core.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/event.h"
#include "core/hle/lock.h"
#include "core/hle/service/hid/hid.h"
#include "core/hle/service/nfp/nfp.h"
#include "core/hle/service/nfp/nfp_user.h"

namespace Service::NFP {

namespace ErrCodes {
constexpr ResultCode ERR_TAG_FAILED(ErrorModule::NFP,
                                    -1); // TODO(ogniK): Find the actual error code
}

Module::Interface::Interface(std::shared_ptr<Module> module, const char* name)
    : ServiceFramework(name), module(std::move(module)) {
    auto& kernel = Core::System::GetInstance().Kernel();
    nfc_tag_load =
        Kernel::Event::Create(kernel, Kernel::ResetType::OneShot, "IUser:NFCTagDetected");
}

Module::Interface::~Interface() = default;

class IUser final : public ServiceFramework<IUser> {
public:
    IUser(Module::Interface& nfp_interface)
        : ServiceFramework("NFP::IUser"), nfp_interface(nfp_interface) {
        static const FunctionInfo functions[] = {
            {0, &IUser::Initialize, "Initialize"},
            {1, &IUser::Finalize, "Finalize"},
            {2, &IUser::ListDevices, "ListDevices"},
            {3, &IUser::StartDetection, "StartDetection"},
            {4, &IUser::StopDetection, "StopDetection"},
            {5, &IUser::Mount, "Mount"},
            {6, &IUser::Unmount, "Unmount"},
            {7, &IUser::OpenApplicationArea, "OpenApplicationArea"},
            {8, &IUser::GetApplicationArea, "GetApplicationArea"},
            {9, nullptr, "SetApplicationArea"},
            {10, nullptr, "Flush"},
            {11, nullptr, "Restore"},
            {12, nullptr, "CreateApplicationArea"},
            {13, &IUser::GetTagInfo, "GetTagInfo"},
            {14, &IUser::GetRegisterInfo, "GetRegisterInfo"},
            {15, &IUser::GetCommonInfo, "GetCommonInfo"},
            {16, &IUser::GetModelInfo, "GetModelInfo"},
            {17, &IUser::AttachActivateEvent, "AttachActivateEvent"},
            {18, &IUser::AttachDeactivateEvent, "AttachDeactivateEvent"},
            {19, &IUser::GetState, "GetState"},
            {20, &IUser::GetDeviceState, "GetDeviceState"},
            {21, &IUser::GetNpadId, "GetNpadId"},
            {22, &IUser::GetApplicationAreaSize, "GetApplicationAreaSize"},
            {23, &IUser::AttachAvailabilityChangeEvent, "AttachAvailabilityChangeEvent"},
            {24, nullptr, "RecreateApplicationArea"},
        };
        RegisterHandlers(functions);

        auto& kernel = Core::System::GetInstance().Kernel();
        deactivate_event =
            Kernel::Event::Create(kernel, Kernel::ResetType::OneShot, "IUser:DeactivateEvent");
        availability_change_event = Kernel::Event::Create(kernel, Kernel::ResetType::OneShot,
                                                          "IUser:AvailabilityChangeEvent");
    }

private:
    struct TagInfo {
        std::array<u8, 10> uuid;
        u8 uuid_length; // TODO(ogniK): Figure out if this is actual the uuid length or does it
                        // mean something else
        INSERT_PADDING_BYTES(0x15);
        u32_le protocol;
        u32_le tag_type;
        INSERT_PADDING_BYTES(0x2c);
    };
    static_assert(sizeof(TagInfo) == 0x54, "TagInfo is an invalid size");

    enum class State : u32 {
        NonInitialized = 0,
        Initialized = 1,
    };

    enum class DeviceState : u32 {
        Initialized = 0,
        SearchingForTag = 1,
        TagFound = 2,
        TagRemoved = 3,
        TagNearby = 4,
        Unknown5 = 5,
        Finalized = 6
    };

    struct CommonInfo {
        u16_be last_write_year;
        u8 last_write_month;
        u8 last_write_day;
        u16_be write_counter;
        u16_be version;
        u32_be application_area_size;
        INSERT_PADDING_BYTES(0x34);
    };
    static_assert(sizeof(CommonInfo) == 0x40, "CommonInfo is an invalid size");

    void Initialize(Kernel::HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2, 0};
        rb.Push(RESULT_SUCCESS);

        state = State::Initialized;

        LOG_DEBUG(Service_NFC, "called");
    }

    void GetState(Kernel::HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 3, 0};
        rb.Push(RESULT_SUCCESS);
        rb.PushRaw<u32>(static_cast<u32>(state));

        LOG_DEBUG(Service_NFC, "called");
    }

    void ListDevices(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const u32 array_size = rp.Pop<u32>();

        ctx.WriteBuffer(&device_handle, sizeof(device_handle));

        LOG_DEBUG(Service_NFP, "called, array_size={}", array_size);

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u32>(1);
    }

    void GetNpadId(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const u64 dev_handle = rp.Pop<u64>();
        LOG_DEBUG(Service_NFP, "called, dev_handle=0x{:X}", dev_handle);
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u32>(npad_id);
    }

    void AttachActivateEvent(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const u64 dev_handle = rp.Pop<u64>();
        LOG_DEBUG(Service_NFP, "called, dev_handle=0x{:X}", dev_handle);
        IPC::ResponseBuilder rb{ctx, 2, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushCopyObjects(nfp_interface.GetNFCEvent());
        has_attached_handle = true;
    }

    void AttachDeactivateEvent(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const u64 dev_handle = rp.Pop<u64>();
        LOG_DEBUG(Service_NFP, "called, dev_handle=0x{:X}", dev_handle);

        IPC::ResponseBuilder rb{ctx, 2, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushCopyObjects(deactivate_event);
    }

    void StopDetection(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_NFP, "called");
        switch (device_state) {
        case DeviceState::TagFound:
        case DeviceState::TagNearby:
            deactivate_event->Signal();
            device_state = DeviceState::Initialized;
            break;
        case DeviceState::SearchingForTag:
        case DeviceState::TagRemoved:
            device_state = DeviceState::Initialized;
            break;
        }
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void GetDeviceState(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_NFP, "called");
        auto nfc_event = nfp_interface.GetNFCEvent();
        if (!nfc_event->ShouldWait(Kernel::GetCurrentThread()) && !has_attached_handle) {
            device_state = DeviceState::TagFound;
            nfc_event->Clear();
        }

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u32>(static_cast<u32>(device_state));
    }

    void StartDetection(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_NFP, "called");

        if (device_state == DeviceState::Initialized || device_state == DeviceState::TagRemoved) {
            device_state = DeviceState::SearchingForTag;
        }
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void GetTagInfo(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_NFP, "called");

        IPC::ResponseBuilder rb{ctx, 2};
        auto amiibo = nfp_interface.GetAmiiboBuffer();
        TagInfo tag_info{};
        std::memcpy(tag_info.uuid.data(), amiibo.uuid.data(), sizeof(tag_info.uuid.size()));
        tag_info.uuid_length = static_cast<u8>(tag_info.uuid.size());

        tag_info.protocol = 1; // TODO(ogniK): Figure out actual values
        tag_info.tag_type = 2;
        ctx.WriteBuffer(&tag_info, sizeof(TagInfo));
        rb.Push(RESULT_SUCCESS);
    }

    void Mount(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_NFP, "called");

        device_state = DeviceState::TagNearby;
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void GetModelInfo(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_NFP, "called");

        IPC::ResponseBuilder rb{ctx, 2};
        auto amiibo = nfp_interface.GetAmiiboBuffer();
        ctx.WriteBuffer(&amiibo.model_info, sizeof(amiibo.model_info));
        rb.Push(RESULT_SUCCESS);
    }

    void Unmount(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_NFP, "called");

        device_state = DeviceState::TagFound;

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void Finalize(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_NFP, "called");

        device_state = DeviceState::Finalized;

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void AttachAvailabilityChangeEvent(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_NFP, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushCopyObjects(availability_change_event);
    }

    void GetRegisterInfo(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_NFP, "(STUBBED) called");

        // TODO(ogniK): Pull Mii and owner data from amiibo

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void GetCommonInfo(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_NFP, "(STUBBED) called");

        // TODO(ogniK): Pull common information from amiibo

        CommonInfo common_info{};
        common_info.application_area_size = 0;
        ctx.WriteBuffer(&common_info, sizeof(CommonInfo));

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void OpenApplicationArea(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_NFP, "called");
        // We don't need to worry about this since we can just open the file
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void GetApplicationAreaSize(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_NFP, "(STUBBED) called");
        // We don't need to worry about this since we can just open the file
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.PushRaw<u32>(0); // This is from the GetCommonInfo stub
    }

    void GetApplicationArea(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_NFP, "(STUBBED) called");

        // TODO(ogniK): Pull application area from amiibo

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.PushRaw<u32>(0); // This is from the GetCommonInfo stub
    }

    bool has_attached_handle{};
    const u64 device_handle{Common::MakeMagic('Y', 'U', 'Z', 'U')};
    const u32 npad_id{0}; // Player 1 controller
    State state{State::NonInitialized};
    DeviceState device_state{DeviceState::Initialized};
    Kernel::SharedPtr<Kernel::Event> deactivate_event;
    Kernel::SharedPtr<Kernel::Event> availability_change_event;
    const Module::Interface& nfp_interface;
};

void Module::Interface::CreateUserInterface(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_NFP, "called");
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushIpcInterface<IUser>(*this);
}

void Module::Interface::LoadAmiibo(const std::vector<u8>& buffer) {
    std::lock_guard<std::recursive_mutex> lock(HLE::g_hle_lock);
    if (buffer.size() < sizeof(AmiiboFile)) {
        return; // Failed to load file
    }
    std::memcpy(&amiibo, buffer.data(), sizeof(amiibo));
    nfc_tag_load->Signal();
}
const Kernel::SharedPtr<Kernel::Event>& Module::Interface::GetNFCEvent() const {
    return nfc_tag_load;
}
const Module::Interface::AmiiboFile& Module::Interface::GetAmiiboBuffer() const {
    return amiibo;
}

void InstallInterfaces(SM::ServiceManager& service_manager) {
    auto module = std::make_shared<Module>();
    std::make_shared<NFP_User>(module)->InstallAsService(service_manager);
}

} // namespace Service::NFP
