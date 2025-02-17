<?php
/*
** Wrapper around api_jsonrpc.php for specific trigger fetching.
** Copyright (C) 2024 OSSO B.V.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/

// Set __LINKFILE__ instead of __FILE__ so we can be symlinked from elsewhere.
define('__LINKFILE__', '/usr/share/zabbix/api_csv.php');

// Reject OPTIONS (the other APIs do too).
if (@$_SERVER['REQUEST_METHOD'] === 'OPTIONS') {
	return;
}

// When called from CLI, we'll pretend it's from localhost.
if (!@$_SERVER['REMOTE_ADDR']) {
	$_SERVER['REQUEST_METHOD'] = 'GET';
	$_SERVER['REMOTE_ADDR'] = '127.0.0.1';
	$_SERVER['HTTP_AUTHORIZATION'] = 'Bearer '.getenv('TOKEN');
}

// This CSV API actually only does GET; for simplicity. The polling device is
// very limited in mem/cpu.
if ($_SERVER['REQUEST_METHOD'] !== 'GET') {
	header('HTTP/1.0 405 Method Not Allowed');
	return;
}

// In api_csv_prelude.php we can do:
//
//  if (@$_SERVER['HTTP_AUTHORIZATION'] == 'Bearer letmein' && in_array(
//  		$_SERVER['REMOTE_ADDR'], array('1.2.3.4', '5.5.5.5''))) {
//  	$_SERVER['HTTP_AUTHORIZATION'] = 'Bearer 123abc';
//  }
//
@include_once 'api_csv_prelude.php';

if (!ob_start()) {
	echo "ERR: no ob_start();\n";
	exit;
}

require_once dirname(__LINKFILE__).'/include/func.inc.php';
require_once dirname(__LINKFILE__).'/include/classes/core/CHttpRequest.php';

class CHttpRequestForJsonRpc extends CHttpRequest {
	public function retrieve_headers($add_headers = false) {
		parent::retrieve_headers($add_headers);
		$this->method = 'POST';
		$this->request_method = 'POST';
	}
}

$http_request = new CHttpRequestForJsonRpc();

require_once dirname(__LINKFILE__).'/include/classes/core/APP.php';

header('Content-Type: text/csv; charset=utf-8');
$data = '{"id": -1}';

try {
	APP::getInstance()->run(APP::EXEC_MODE_API);

	$apiClient = API::getWrapper()->getClient();

	// unset wrappers so that calls between methods would be made directly to the services
	API::setWrapper();

	function jsonrpc_call($data) {
		global $apiClient, $http_request;
		$jsonRpc = new CJsonRpc($apiClient, $data);
		$output = $jsonRpc->execute($http_request);
		$values = json_decode($output);
		if (property_exists($values, 'error')) {
			throw new Exception($values->error->data);
		}
		return $values->result;
	}

	$action = 'get_triggers';
	if ($action == 'get_triggers') {
		$data = '{
		  "jsonrpc":"2.0", "method":"problem.get", "id": 1,
		  "params":{
		    "output":["eventid","r_eventid","objectid","clock","ns","severity","suppressed","name"],
		    "source":0,"object":0,"recent":false,
		    "severities":[5]
		  }
		}';
		$disaster_triggers = jsonrpc_call($data);

		$event_map = array();
		$trigger_map = array();
		foreach ($disaster_triggers as $k => $obj) {
			if (!array_key_exists((int)$obj->objectid, $trigger_map)) {
				$trigger_map[(int)$obj->objectid] = array();
			}
			array_push($trigger_map[(int)$obj->objectid], $obj);
			array_push($event_map, (int)$obj->eventid);
		}

		$data = '{
		  "jsonrpc":"2.0", "method":"event.get", "id": 1,
                  "params":{
		    "output":["eventid","acknowledged"],
		    "eventids":[' . implode(",", array_values($event_map)) . ']
		  }
		}';
		$events = jsonrpc_call($data);
		$event_ack = array();
		foreach ($events as $k => $obj) {
			if ($obj->acknowledged) {
				$event_ack[(int)$obj->eventid] = true;
			}
		}

		$data = '{
		  "jsonrpc":"2.0", "method":"trigger.get", "id": 2,
		  "params":{
		    "output":["triggerid","status","error","suppressed","flags","value"],
		    "selectHosts":["hostid","host","status"],
		    "selectItems":["hostid","status"],
		    "triggerids":[' . implode(",", array_keys($trigger_map)) . ']
		  }
		}';
		$trigger_info = jsonrpc_call($data);

		$triggers = array();
		foreach ($trigger_info as $k => $obj) {
			if ($obj->status == 1) {
				// Trigger is disabled. Ignore.
				continue;
			}
			$trigger_events = $trigger_map[$obj->triggerid];
			foreach ($obj->hosts as $k2 => $host) {
				if ($host->status == 1) {
					// Host is disabled. Ignore.
					continue;
				}
				foreach ($trigger_events as $event) {
					// Trigger is ACKed, suppress it.
					$is_acked = array_key_exists((int)$event->eventid, $event_ack) ? 1 : 0;
					$trigger = array(
						'clock' => $event->clock,
						'severity' => $event->severity,
						'suppressed' => $event->suppressed | $is_acked,
						'hostid' => $host->hostid,
						'host' => '', // $host->host,
						'name' => '', // $event->name,
					);
					array_push($triggers, $trigger);
				}
			}
		}

		// Sort the output. Unsuppressed first. Then newest first.
		usort($triggers, function($a, $b) {
			if ($a['suppressed'] != $b['suppressed']) {
				return $b['suppressed'] ? -1 : 1;
			}
			if ($a['clock'] != $b['clock']) {
				return $a['clock'] < $b['clock'] ? 1 : -1;
			}
			return strcmp(implode("\0", $a), implode("\0", $b));
		});

		$csv_output = array();

		// Add example triggers.
		for ($i = 0; $i < 0; ++$i) {
			array_push($triggers, array(
				'clock' => 1734081320 + $i,
				'severity' => 5,
				'suppressed' => 0,
				'hostid' => 12345,
				'host' => '', // 'sentry.dr.example.com',
				'name' => '', // Free disk space on sentry.dr.example.com is less than 10% on volume /
			));
		}

		if (empty($triggers)) {
			// Always non-zero output (right now PIM fails on content-length 0)
			//array_push($csv_output, "void");
		} else {
			array_push($csv_output, implode(";", array_keys($triggers[0])));
		}
		foreach ($triggers as $trigger) {
			array_push($csv_output, implode(";", $trigger));
		}
		$csv_output = implode("\n", $csv_output);
		if (!empty($csv_output)) {
			$csv_output .= "\n";
		}
	}
}
catch (Exception $e) {
	// decode input json request to get request's id
	$jsonData = json_decode($data, true);

	$response = [
		'jsonrpc' => '2.0',
		'error' => [
			'code' => 1,
			'message' => $e->getMessage(),
			'data' => ''
		],
		'id' => (isset($jsonData['id']) ? $jsonData['id'] : null)
	];
	$csv_output = '';
	$csv_output .= implode(';', array('jsonrpc', 'error.code', 'error.message', 'error.data', 'id')) . "\n";
	$csv_output .= '2.0;' . implode(';', $response['error']) . ";$response[id]@$_SERVER[REMOTE_ADDR]\n";
}

// One output. Except we write it to the buffer.
echo $csv_output;

// PHP is smart enough to disable "Transfer-Encoding: chunked" once the
// content-length is set.
// NOTE: We use ob_start/ob_get_clean to make sure we capture EVERYTHING. If
// there are stray errors or prints, we need them too.
$real_output = ob_get_clean();
// We need to set this because the HTTP client in the PIM670 device is
// rather dumb.
header('Content-Length: ' . strlen($real_output));
echo $real_output;

session_write_close();  // FIXME: not sure if we want this.. we don't need any session data
// vim: set ts=8 sw=8 sts=8 noet ai:
