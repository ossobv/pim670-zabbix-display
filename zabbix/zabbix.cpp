// vim: set ts=8 sw=4 sts=4 et ai:
#include "zabbix.hpp"

#include "tiny-json.h"

#ifdef RUNTESTS
#include <iostream>
#endif

// Maximum tokens for TinyJSON.
// If this is set too low, we cannot parse json docs.
#define MAX_JSON_TOKENS (64 * 1024)

#if 1
namespace zabbix {
#endif

inline void extractProblemData(
        std::string problems,
        std::vector<int>& problem_objectids,
        std::vector<uint64_t>& problem_times,
        std::vector<int>& problem_severities,
        std::vector<std::string>& problem_descriptions,
        std::vector<bool>& problem_suppresseds);
inline int getTriggerTriggerid(json_t const* result);
inline bool getTriggerStatus(json_t const* result);
inline std::string getTriggerHost(json_t const* result);

std::vector<Alert> JsonRpcApi::getAlerts() {
    std::vector<Alert> alerts;

    std::string problems = fetchProblemData();

    std::vector<int> problem_objectids;
    std::vector<uint64_t> problem_times;
    std::vector<int> problem_severities;
    std::vector<std::string> problem_descriptions;
    std::vector<bool> problem_suppresseds;

    // NOTE: Beware, problems gets mangled and is now unusable!
    extractProblemData(
        problems, problem_objectids, problem_times,
        problem_severities, problem_descriptions,
        problem_suppresseds);

    std::string triggers = fetchTriggerData(problem_objectids);

    // FIXME: What about these MAX_JSON_TOKENS?
    json_t pool[MAX_JSON_TOKENS];
    // NOTE: Beware, triggers gets mangled and is now unusable!
    const json_t* root = json_create(triggers.data(), pool, MAX_JSON_TOKENS);
    if (!root) {
        // FIXME: error?
        return alerts;
    }

    // FIXME: runtime_errors? We do not like throws in embedded systems.
    json_t const* resultArray = json_getProperty(root, "result");
    if (!resultArray || json_getType(resultArray) != JSON_ARRAY) {
        // FIXME: error?
        return alerts;
    }

    json_t const* obj;
    unsigned start_objectids = 0;
    for (obj = json_getChild(resultArray); obj; obj = json_getSibling(obj)) {
        int triggerid = getTriggerTriggerid(obj);
        bool is_disabled = getTriggerStatus(obj);
        if (!is_disabled) {
            std::string host = getTriggerHost(obj); // take first non-Disabled host
            if (!host.empty()) {
                // Find triggerid in objectids
                // use its values
                // drop it from the lists.
                for (unsigned i = start_objectids; i < problem_objectids.size(); ++i) {
                    if (problem_objectids[i] == triggerid) {
                        alerts.push_back(Alert{
                            problem_times[i],
                            problem_severities[i],
                            host,
                            problem_descriptions[i],
                            problem_suppresseds[i],
                        });
                        // Optimize the next searches if the first
                        // result was what we were looking for.
                        if (i == start_objectids) {
                            ++start_objectids;
                        }
                        break;
                    }
                }
            }
        }
    }

    return alerts;
}

/**
 * Fetch problems.
 *
 * Output:
 *  {
 *    "jsonrpc": "2.0",
 *    "result": [
 *      {
 *        "eventid": "57019172",
 *        "r_eventid": "0",
 *        "objectid": "895628",
 *        "clock": "1698407318",
 *        "ns": "0",
 *        "severity": "5",
 *        "name": "Zabbix agent on wg2.example.com is unreachable for 5 minutes",
 *        "suppressed": "0"
 *      }
 *    ]
 *  }
 */
std::string JsonRpcApi::fetchProblemData() {
    // Create JSON-RPC request
    std::string request = R"({
        "jsonrpc":"2.0",
        "method":"problem.get",
        "params":{
            "output":["eventid","r_eventid","objectid","clock","ns","severity","suppressed","name"],
            "source":0,"object":0,"recent":false,
            "severities":[5]
        }, "id": 1
    })";

    // Send request
    return _jsonrpc_request(request);
}

/**
 * Fetch trigger data for problems.
 *
 * Output:
 *  {
 *    "jsonrpc": "2.0",
 *    "result": [
 *      {
 *        "triggerid": "895628",
 *        "status": "0",
 *        "error": "",
 *        "flags": "0",
 *        "value": "1",
 *        "hosts": [
 *          {
 *            "hostid": "12110",
 *            "host": "wg2.example.com",
 *            "status": "1"
 *          }
 *        ],
 *        "items": [
 *          {
 *            "itemid": "1768391",
 *            "hostid": "12110",
 *            "status": "0"
 *          }
 *        ]
 *      }
 *    ]
 *  }
 */
std::string JsonRpcApi::fetchTriggerData(const std::vector<int>& objectIds) {
    // Create JSON-RPC request
    // TODO: Maybe we can skip selectItems?
    std::string request = R"({
        "jsonrpc":"2.0",
        "method":"trigger.get",
        "params":{
            "output":["triggerid","status","error","suppressed","flags","value"],
            "selectHosts":["hostid","host","status"],
            "selectItems":["hostid","status"],
        "triggerids":[)";
    for (size_t i = 0; i < objectIds.size(); i++) {
        if (i)
            request += ",";
        request += std::to_string(objectIds[i]);
    }
    request += R"(]},"id":2})";

    // Send request
    return _jsonrpc_request(request);
}

inline void extractProblemData(
        std::string problems,
        std::vector<int>& problem_objectids,
        std::vector<uint64_t>& problem_times,
        std::vector<int>& problem_severities,
        std::vector<std::string>& problem_descriptions,
        std::vector<bool>& problem_suppresseds) {

    // FIXME: What about these MAX_JSON_TOKENS?
    json_t pool[MAX_JSON_TOKENS];
    const json_t* root = json_create(problems.data(), pool, MAX_JSON_TOKENS);
    if (!root) {
        // FIXME: error?
        return;
    }

    // FIXME: runtime_errors? We do not like throws in embedded systems.
    json_t const* resultArray = json_getProperty(root, "result");
    if (!resultArray || json_getType(resultArray) != JSON_ARRAY) {
        // FIXME: error?
        return;
    }

    json_t const* obj;
    for (obj = json_getChild(resultArray); obj; obj = json_getSibling(obj)) {
        {
            json_t const* objectid = json_getProperty(obj, "objectid");
            if (objectid && json_getType(objectid) == JSON_TEXT) {
                problem_objectids.push_back(std::stoi(json_getValue(objectid)));
            } else {
                problem_objectids.push_back(0);
            }
        }
        {
            json_t const* time = json_getProperty(obj, "clock");
            if (time && json_getType(time) == JSON_TEXT) {
                problem_times.push_back(std::stoi(json_getValue(time)));
            } else {
                problem_times.push_back(0);
            }
        }
        {
            json_t const* severity = json_getProperty(obj, "severity");
            if (severity && json_getType(severity) == JSON_TEXT) {
                problem_severities.push_back(std::stoi(json_getValue(severity)));
            } else {
                problem_severities.push_back(0);
            }
        }
        {
            json_t const* description = json_getProperty(obj, "name");
            if (description && json_getType(description) == JSON_TEXT) {
                problem_descriptions.push_back(json_getValue(description));
            } else {
                problem_descriptions.push_back("");
            }
        }
        {
            json_t const* suppressed = json_getProperty(obj, "suppressed");
            if (suppressed && json_getType(suppressed) == JSON_TEXT) {
                problem_suppresseds.push_back((bool)std::stoi(json_getValue(suppressed)));
            } else {
                problem_suppresseds.push_back(false);
            }
        }
    }
}

inline int getTriggerTriggerid(json_t const* result) {
    int ret = -1;
    json_t const* triggerid = json_getProperty(result, "triggerid");
    if (triggerid && json_getType(triggerid) == JSON_TEXT) {
        ret = std::stoi(json_getValue(triggerid));
    }
    return ret;
}

inline bool getTriggerStatus(json_t const* result) {
    int ret = false;
    json_t const* status = json_getProperty(result, "status");
    if (status && json_getType(status) == JSON_TEXT) {
        ret = (bool)std::stoi(json_getValue(status));
    }
    return ret;
}

inline std::string getTriggerHost(json_t const* result) {
    std::string hostname;
    json_t const* hostArray = json_getProperty(result, "hosts");
    if (!hostArray || json_getType(hostArray) != JSON_ARRAY) {
        // FIXME: error?
        return "";
    }
    json_t const* obj;
    for (obj = json_getChild(hostArray); obj; obj = json_getSibling(obj)) {
        json_t const* host = json_getProperty(obj, "host");
        if (host && json_getType(host) == JSON_TEXT) {
            hostname = json_getValue(host);
        }
        json_t const* status = json_getProperty(obj, "status");
        if (status && json_getType(status) == JSON_TEXT) {
            // host.status == 0 --> host is enabled
            if (std::stoi(json_getValue(status)) == 0) {
                return hostname;
            }
        }
    }
    return "";
}


#if 1
} //namespace zabbix
#endif

#ifdef RUNTESTS
class MockHttp {
    int callCount;

public:
    MockHttp() : callCount(0) {}

    std::string operator()(const std::string& request) {
        ++callCount;
        if (callCount == 1) {
            return R"({
                "jsonrpc": "2.0",
                "result": [
                    {
                        "eventid": "57019171",
                        "r_eventid": "0",
                        "objectid": "895627",
                        "clock": "1698407317",
                        "ns": "0",
                        "severity": "5",
                        "name": "Zabbix agent on wg1.example.com is unreachable for 5 minutes",
                        "suppressed": "0"
                    },
                    {
                        "eventid": "57019172",
                        "r_eventid": "0",
                        "objectid": "895628",
                        "clock": "1698407318",
                        "ns": "0",
                        "severity": "5",
                        "name": "Zabbix agent on (disabled) wg2.example.com is unreachable for 5 minutes",
                        "suppressed": "0"
                    },
                    {
                        "eventid": "55113316",
                        "r_eventid": "0",
                        "objectid": "1011770",
                        "clock": "1689492538",
                        "ns": "935748717",
                        "severity": "5",
                        "name": "CPU 25+% busy with I/O for >1h on ch03.example.com",
                        "suppressed": "0"
                    },
                    {
                        "eventid": "55113317",
                        "r_eventid": "0",
                        "objectid": "1011771",
                        "clock": "1689492539",
                        "ns": "935748717",
                        "severity": "5",
                        "name": "(disabled trigger) CPU 25+% busy with I/O for >1h on ch04.example.com",
                        "suppressed": "0"
                    }
                ],
                "id": 2
            })";
        } else if (callCount == 2) {
            return R"({
                "jsonrpc": "2.0",
                "result": [
                    {
                        "triggerid": "895627",
                        "status": "0",
                        "error": "",
                        "flags": "0",
                        "value": "1",
                        "hosts": [
                            {
                                "hostid": "12109",
                                "host": "wg1.example.com",
                                "status": "0"
                            }
                        ],
                        "items": [
                            {
                                "itemid": "1768390",
                                "hostid": "12109",
                                "status": "0"
                            }
                        ]
                    },
                    {
                        "triggerid": "895628",
                        "status": "0",
                        "error": "",
                        "flags": "0",
                        "value": "1",
                        "hosts": [
                            {
                                "hostid": "12110",
                                "host": "wg2.example.com",
                                "status": "1"
                            }
                        ],
                        "items": [
                            {
                                "itemid": "1768391",
                                "hostid": "12110",
                                "status": "0"
                            }
                        ]
                    },
                    {
                        "triggerid": "1011770",
                        "status": "0",
                        "error": "",
                        "flags": "0",
                        "value": "1",
                        "hosts": [
                            {
                                "hostid": "12384",
                                "host": "ch03.example.com",
                                "status": "0"
                            }
                        ],
                        "items": [
                            {
                                "itemid": "2038021",
                                "hostid": "12384",
                                "status": "0"
                            }
                        ]
                    },
                    {
                        "triggerid": "1011771",
                        "status": "1",
                        "error": "",
                        "flags": "0",
                        "value": "1",
                        "hosts": [
                            {
                                "hostid": "12385",
                                "host": "ch04.example.com",
                                "status": "0"
                            }
                        ],
                        "items": [
                            {
                                "itemid": "2038022",
                                "hostid": "12385",
                                "status": "0"
                            }
                        ]
                    }
                ],
                "id": 2
            })";
        }
        return "{}"; // Empty JSON for further calls
    }
};
#endif //RUNTESTS

#ifdef RUNTESTS
int main() {
    MockHttp mock_http;
    zabbix::JsonRpcApi zabbix_api(mock_http);
    std::vector<zabbix::Alert> alerts = zabbix_api.getAlerts();
    for (zabbix::Alert alert : alerts) {
        // wg1.example.com (not wg2, because host is disabled)
        // ch03.example.com (not ch04, because trigger is disabled)
        std::cout << alert.host() << std::endl;
    }
    return 0;
}
#endif //RUNTESTS
