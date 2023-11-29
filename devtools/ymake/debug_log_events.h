#pragma once

#include <variant>

#include "add_iter_debug.h"
#include "export_json_debug.h"
#include "induced_props.h"
#include "md5_debug.h"

#define DEBUG_EVENT_TYPES \
NDebugEvents::TMd5Change, \
NDebugEvents::NExportJson::TCacheSearch, \
NDebugEvents::NIter::TEnterEvent, \
NDebugEvents::NIter::TLeaveEvent, \
NDebugEvents::NIter::TLeftEvent, \
NDebugEvents::NIter::TRawPopEvent, \
NDebugEvents::NIter::TStartEditEvent, \
NDebugEvents::NIter::TRescanEvent, \
NDebugEvents::NIter::TNotReadyIntents, \
NDebugEvents::NIter::TSetupRequiredIntents, \
NDebugEvents::NIter::TSetupReceiveFromChildIntents, \
NDebugEvents::NIter::TResetFetchIntents, \
NDebugEvents::NProperties::TAddEvent, \
NDebugEvents::NProperties::TClearEvent, \
NDebugEvents::NProperties::TReadEvent, \
NDebugEvents::NProperties::TUseEvent

using TDebugLogEvent = std::variant<DEBUG_EVENT_TYPES>;
