// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "stdafx.h"
#include "PluginJsonConstants.h"
#include "device-agent/common/plugins/PluginConstants.h"
#include "TimeStateHandler.h"

using namespace DMCommon;
using namespace DMUtils;
using namespace std;

constexpr char InterfaceVersion[] = "1.0.0";

namespace Microsoft { namespace Azure { namespace DeviceManagement { namespace TimePlugin {

    const char* NtpServerPropertyName = "NtpServer";

    TimeStateHandler::TimeStateHandler() :
        HandlerBase(TimeStateHandlerId, ReportedSchema(JsonDeviceSchemasTypeRaw, JsonDeviceSchemasTagDM, InterfaceVersion))
    {
        _groupDesiredConfigJson = Json::Value(Json::objectValue);
    }

    string TimeStateHandler::GetNtpServer()
    {
        unsigned long returnCode;
        std::string output;
        Process::Launch(L"C:\\windows\\system32\\w32tm.exe /query /configuration", returnCode, output);
        if (returnCode != 0)
        {
            throw DMException(DMSubsystem::W32TM, returnCode, "Error: w32tm.exe returned an error code.");
        }

        string ntpServer;
        vector<string> lines;
        Utils::SplitString(output, '\n', lines);
        for (const string& line : lines)
        {
            vector<string> tokens;
            DMUtils::SplitString(line, ':', tokens);
            if (tokens.size() != 2)
            {
                continue;
            }

            string name = DMUtils::TrimString(tokens[0], string(" "));
            string value = DMUtils::TrimString(tokens[1], string(" "));

            if (name == NtpServerPropertyName)
            {
                // remove the trailing " (Local)".
                size_t pos = value.find(" (Local)");
                if (pos != -1)
                {
                    value = value.replace(pos, value.length() - pos, "");
                }

                pos = value.find(",");
                if (pos != -1)
                {
                    value = value.replace(pos, value.length() - pos, "");
                }

                ntpServer = value;
                break;
            }
        }

        return ntpServer;
    }

    void TimeStateHandler::SetNtpServer(const std::string& ntpServer)
    {
        wstring command;
        command += L"w32tm /config /manualpeerlist:";
        command += Utils::MultibyteToWide(ntpServer.c_str());
        command += L" /syncfromflags:manual /reliable:yes /update";

        unsigned long returnCode = 0;
        string output;
        Process::Launch(command, returnCode, output);
        if (returnCode != 0)
        {
            throw DMException(DMSubsystem::W32TM, returnCode, "Error: w32tm.exe returned an error.");
        }
    }

    void TimeStateHandler::GetSubGroupNtpServer(
        Json::Value& reportedObject,
        std::shared_ptr<DMCommon::ReportedErrorList> errorList)
    {
        Operation::RunOperation(JsonTimeInfoNtpServer, errorList,
            [&]()
        {
            reportedObject[JsonTimeInfoNtpServer] = GetNtpServer();
        });
    }

    void TimeStateHandler::SetSubGroupNtpServer(
        const Json::Value& groupDesiredConfigJson,
        std::shared_ptr<DMCommon::ReportedErrorList> errorList)
    {
        Operation::RunOperation(JsonTimeInfoNtpServer, errorList,
            [&]()
        {
            OperationModelT<string> ntpServerModel = Operation::TryGetStringJsonValue(groupDesiredConfigJson, JsonTimeInfoNtpServer);
            if (ntpServerModel.present)
            {
                SetNtpServer(ntpServerModel.value);
                _isConfigured = true;
            }
        });
    }

    void TimeStateHandler::GetSubGroupTimeZone(
        Json::Value& reportedObject,
        std::shared_ptr<DMCommon::ReportedErrorList> errorList)
    {
        Operation::RunOperation(JsonTimeZoneReportCfgCtx, errorList,
            [&]()
        {
            DYNAMIC_TIME_ZONE_INFORMATION tzi = { 0 };
            if (TIME_ZONE_ID_INVALID == GetDynamicTimeZoneInformation(&tzi))
            {
                throw DMException(DMSubsystem::Windows, GetLastError(), "Error: failed to retrieve time zone information.");
            }

            reportedObject[JsonDynamicDaylightTimeDisabled] = tzi.DynamicDaylightTimeDisabled ? Json::Value(true) : Json::Value(false);
            reportedObject[JsonTimeZoneKeyName] = Json::Value(WideToMultibyte(tzi.TimeZoneKeyName));
            reportedObject[JsonTimeZoneBias] = Json::Value(tzi.Bias);

            reportedObject[JsonTimeZoneDaylightBias] = Json::Value(tzi.DaylightBias);
            reportedObject[JsonTimeZoneDaylightDate] = Json::Value(WideToMultibyte(DateTime::ISO8601FromSystemTime(tzi.DaylightDate).c_str()));
            reportedObject[JsonTimeZoneDaylightName] = Json::Value(WideToMultibyte(tzi.DaylightName));
            reportedObject[JsonTimeZoneDaylightDayOfWeek] = Json::Value(tzi.DaylightDate.wDayOfWeek);

            reportedObject[JsonTimeZoneStandardBias] = Json::Value(tzi.StandardBias);
            reportedObject[JsonTimeZoneStandardDate] = Json::Value(WideToMultibyte(DateTime::ISO8601FromSystemTime(tzi.StandardDate).c_str()));
            reportedObject[JsonTimeZoneStandardName] = Json::Value(WideToMultibyte(tzi.StandardName));
            reportedObject[JsonTimeZoneStandardDayOfWeek] = Json::Value(tzi.StandardDate.wDayOfWeek);
        });

    }

    void TimeStateHandler::SetSubGroupTimeZone(
        const Json::Value& groupDesiredConfigJson,
        std::shared_ptr<DMCommon::ReportedErrorList> errorList)
    {
        TRACELINE(LoggingLevel::Verbose, __FUNCTION__);

        Operation::RunOperation(JsonTimeZoneReadCfgCtx, errorList,
            [&]()
        {
            // Parse...
            unsigned int fieldCount = 0;

            OperationModelT<bool> dynamicDaylightTimeDisabledModel = Operation::TryGetBoolJsonValue(groupDesiredConfigJson, JsonDynamicDaylightTimeDisabled);
            OperationModelT<string> timeZoneKeyNameModel = Operation::TryGetStringJsonValue(groupDesiredConfigJson, JsonTimeZoneKeyName);
            OperationModelT<int> timeZoneBiasModel = Operation::TryGetIntJsonValue(groupDesiredConfigJson, JsonTimeZoneBias);

            OperationModelT<int> timeZoneDaylightBiasModel = Operation::TryGetIntJsonValue(groupDesiredConfigJson, JsonTimeZoneDaylightBias);
            OperationModelT<string> timeZoneDaylightDateModel = Operation::TryGetStringJsonValue(groupDesiredConfigJson, JsonTimeZoneDaylightDate);
            OperationModelT<string> timeZoneDaylightNameModel = Operation::TryGetStringJsonValue(groupDesiredConfigJson, JsonTimeZoneDaylightName);
            OperationModelT<int> timeZoneDaylightDayOfWeekModel = Operation::TryGetIntJsonValue(groupDesiredConfigJson, JsonTimeZoneDaylightDayOfWeek);

            OperationModelT<int> timeZoneStandardBiasModel = Operation::TryGetIntJsonValue(groupDesiredConfigJson, JsonTimeZoneStandardBias);
            OperationModelT<string> timeZoneStandardDateModel = Operation::TryGetStringJsonValue(groupDesiredConfigJson, JsonTimeZoneStandardDate);
            OperationModelT<string> timeZoneStandardNameModel = Operation::TryGetStringJsonValue(groupDesiredConfigJson, JsonTimeZoneStandardName);
            OperationModelT<int> timeZoneStandardDayOfWeekModel = Operation::TryGetIntJsonValue(groupDesiredConfigJson, JsonTimeZoneStandardDayOfWeek);

            fieldCount += dynamicDaylightTimeDisabledModel.present ? 1 : 0;
            fieldCount += timeZoneKeyNameModel.present ? 1 : 0;
            fieldCount += timeZoneBiasModel.present ? 1 : 0;

            fieldCount += timeZoneDaylightBiasModel.present ? 1 : 0;
            fieldCount += timeZoneDaylightDateModel.present ? 1 : 0;
            fieldCount += timeZoneDaylightNameModel.present ? 1 : 0;
            fieldCount += timeZoneDaylightDayOfWeekModel.present ? 1 : 0;

            fieldCount += timeZoneStandardBiasModel.present ? 1 : 0;
            fieldCount += timeZoneStandardDateModel.present ? 1 : 0;
            fieldCount += timeZoneStandardNameModel.present ? 1 : 0;
            fieldCount += timeZoneStandardDayOfWeekModel.present ? 1 : 0;

            // Only all or nothing are allowed...
            if (fieldCount != 11 and fieldCount != 0)
            {
                throw DMException(DMSubsystem::DeviceAgentPlugin, DM_ERROR_INVALID_JSON_FORMAT, "Missing time zone fields");
            }

            // If nothing, return...
            if (fieldCount == 0)
            {
                TRACELINE(LoggingLevel::Verbose, "No time zone fields are defined. Returning.");
                return;
            }

            // Is configured?
            _isConfigured = true;

            // If we have all the fields, then configure...
            DYNAMIC_TIME_ZONE_INFORMATION tzi = { 0 };

            // Use registry settings?
            tzi.DynamicDaylightTimeDisabled = dynamicDaylightTimeDisabledModel.value ? TRUE : FALSE;

            // Option 1: If dynamicDaylightTimeDisabled = false, look-up the TimeZoneKeyName...
            wcsncpy_s(tzi.TimeZoneKeyName, MultibyteToWide(timeZoneKeyNameModel.value.c_str()).c_str(), _TRUNCATE);

            // Option 2: If dynamicDaylightTimeDisabled = true || timeZoneKeyName is not found, using the following time zone spec...

            // Bias...
            tzi.Bias = timeZoneBiasModel.value;

            // Standard...
            wcsncpy_s(tzi.StandardName, MultibyteToWide(timeZoneStandardNameModel.value.c_str()).c_str(), _TRUNCATE);
            wstring timeZoneStandardDate = MultibyteToWide(timeZoneStandardDateModel.value.c_str());
            if (timeZoneStandardDate.size() == 0)
            {
                // No support for daylight saving time.
                tzi.StandardDate.wMonth = 0;
            }
            else
            {
                DateTime::SystemTimeFromISO8601(timeZoneStandardDate, tzi.StandardDate);
            }
            tzi.StandardDate.wDayOfWeek = static_cast<WORD>(timeZoneStandardDayOfWeekModel.value);
            tzi.StandardBias = timeZoneStandardBiasModel.value;

            // Daytime...
            wcsncpy_s(tzi.DaylightName, MultibyteToWide(timeZoneDaylightNameModel.value.c_str()).c_str(), _TRUNCATE);
            wstring timeZoneDaylightDate = MultibyteToWide(timeZoneStandardDateModel.value.c_str());
            if (timeZoneDaylightDate.size() == 0)
            {
                // No support for daylight saving time.
                tzi.DaylightDate.wMonth = 0;
            }
            else
            {
                DateTime::SystemTimeFromISO8601(timeZoneDaylightDate.c_str(), tzi.DaylightDate);
            }
            tzi.DaylightDate.wDayOfWeek = static_cast<WORD>(timeZoneDaylightDayOfWeekModel.value);
            tzi.DaylightBias = timeZoneDaylightBiasModel.value;

            // Set it...
            if (!SetDynamicTimeZoneInformation(&tzi))
            {
                throw DMException(DMSubsystem::Windows, GetLastError(), "Error: failed to set time zone information.");
            }
        });
    }

    void TimeStateHandler::BuildReported(
        Json::Value& reportedObject,
        std::shared_ptr<DMCommon::ReportedErrorList> errorList)
    {
        GetSubGroupNtpServer(reportedObject, errorList);
        GetSubGroupTimeZone(reportedObject, errorList);
    }

    void TimeStateHandler::EmptyReported(
        Json::Value& reportedObject)
    {
        Json::Value nullValue;

        // Ntp Server
        reportedObject[JsonTimeInfoNtpServer] = nullValue;

        // Time Zone
        reportedObject[JsonDynamicDaylightTimeDisabled] = nullValue;
        reportedObject[JsonTimeZoneKeyName] = nullValue;
        reportedObject[JsonTimeZoneBias] = nullValue;

        reportedObject[JsonTimeZoneDaylightBias] = nullValue;
        reportedObject[JsonTimeZoneDaylightDate] = nullValue;
        reportedObject[JsonTimeZoneDaylightName] = nullValue;
        reportedObject[JsonTimeZoneDaylightDayOfWeek] = nullValue;

        reportedObject[JsonTimeZoneStandardBias] = nullValue;
        reportedObject[JsonTimeZoneStandardDate] = nullValue;
        reportedObject[JsonTimeZoneStandardName] = nullValue;
        reportedObject[JsonTimeZoneStandardDayOfWeek] = nullValue;
    }

    void TimeStateHandler::Start(
        const Json::Value& handlerConfig,
        bool& active)
    {
        TRACELINE(LoggingLevel::Verbose, __FUNCTION__);

        SetConfig(handlerConfig);

        Json::Value logFilesPath = handlerConfig[JsonTextLogFilesPath];
        if (!logFilesPath.isNull() && logFilesPath.isString())
        {
            wstring wideLogFileName = MultibyteToWide(logFilesPath.asString().c_str());
            wstring wideLogFileNamePrefix = MultibyteToWide(GetId().c_str());
            gLogger.SetLogFilePath(wideLogFileName.c_str(), wideLogFileNamePrefix.c_str());
            gLogger.EnableConsole(true);

            TRACELINE(LoggingLevel::Verbose, "Logging configured.");
        }

        active = true;
    }

    void TimeStateHandler::OnConnectionStatusChanged(
        ConnectionStatus status)
    {
        TRACELINE(LoggingLevel::Verbose, __FUNCTION__);
        if (status == ConnectionStatus::eOffline)
        {
            TRACELINE(LoggingLevel::Verbose, "Connection Status: Offline.");
        }
        else
        {
            TRACELINE(LoggingLevel::Verbose, "Connection Status: Online.");
        }
    }

    InvokeResult TimeStateHandler::Invoke(
        const Json::Value& groupDesiredConfigJson) noexcept
    {
        TRACELINE(LoggingLevel::Verbose, __FUNCTION__);

        // Returned objects (if InvokeContext::eDirectMethod, it is returned to the cloud direct method caller).
        InvokeResult invokeResult(InvokeContext::eDesiredState);

        // Twin reported objects
        Json::Value reportedObject(Json::objectValue);
        std::shared_ptr<ReportedErrorList> errorList = make_shared<ReportedErrorList>();

        Operation::RunOperation(GetId(), errorList,
            [&]()
        {
            // Make sure this is not a transient state
            if (Operation::IsRefreshing(groupDesiredConfigJson))
            {
                return;
            }

            // Merge...
            // ToDo: Note that this merge causes the loss of which parts are being
            //       set now. For example, if the ntp server was configured earlier
            //       (i.e. cached), and this change doesn't include it, the SetSubGroup()
            //       will still set it again.
            JsonHelpers::Merge(groupDesiredConfigJson, _groupDesiredConfigJson);

            // Processing Meta Data
            _metaData->FromJsonParentObject(groupDesiredConfigJson);
            string serviceInterfaceVersion = _metaData->GetServiceInterfaceVersion();
            
            // Report refreshing
            ReportRefreshing();

            //Compare interface version with the interface version sent by service
            if (MajorVersionCompare(InterfaceVersion, serviceInterfaceVersion) == 0)
            {
                // Apply new state
                SetSubGroupNtpServer(_groupDesiredConfigJson, errorList);
                SetSubGroupTimeZone(_groupDesiredConfigJson, errorList);

                // Report current state
                if (_metaData->GetReportingMode() == JsonReportingModeDefault)
                {
                    BuildReported(reportedObject, errorList);
                }
                else
                {
                    EmptyReported(reportedObject);
                }
                _metaData->SetDeviceInterfaceVersion(InterfaceVersion);
            }
            else
            {
                throw DMException(DMSubsystem::DeviceAgentPlugin, DM_PLUGIN_ERROR_INVALID_INTERFACE_VERSION, "Service solution is trying to talk with Interface Version that is not supported.");
            }
        });

        // Update device twin
        FinalizeAndReport(reportedObject, errorList);

        return invokeResult;
    }

}}}}