#ifndef INCLUDED_ZABBIX_HPP
#define INCLUDED_ZABBIX_HPP

#include <functional>
#include <string>
#include <vector>

namespace zabbix {

class Alert {
    uint64_t _time;
    int _severity;
    std::string _host;
    std::string _description;
    bool _suppressed;

public:
    Alert(uint64_t time, int severity, const std::string& host, const std::string& description, bool suppressed)
        : _time(time), _severity(severity), _host(host), _description(description), _suppressed(suppressed) {}
    inline const int severity() const {
        return _severity;
    }
    inline const std::string& host() const {
        return _host;
    }
    inline const std::string& description() const {
        return _description;
    }
    inline const bool suppressed() const {
        return _suppressed;
    }
};

class JsonRpcApi {
    std::function<std::string(const std::string&)> _jsonrpc_request;

public:
    /**
     * FIXME: This does not work. We cannot work with a request like
     * that, because we have to return to the main loop after doing the
     * request, before getting the response.
     */
    JsonRpcApi(std::function<std::string(const std::string&)> jsonrpc_request)
        : _jsonrpc_request(jsonrpc_request) {}

    std::vector<Alert> getAlerts();

private:
    std::string fetchProblemData();
    std::string fetchTriggerData(const std::vector<int>& objectIds);
};

} //namespace zabbix

#endif //INCLUDED_ZABBIX_HPP
