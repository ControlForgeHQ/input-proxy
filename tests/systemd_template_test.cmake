if(NOT DEFINED INPUT_PROXY_SYSTEMD_TEMPLATE)
    message(FATAL_ERROR "INPUT_PROXY_SYSTEMD_TEMPLATE is required")
endif()

file(READ "${INPUT_PROXY_SYSTEMD_TEMPLATE}" template)

foreach(required_line IN ITEMS
    "[Unit]"
    "[Service]"
    "Type=simple"
    "User=input-proxy"
    "Group=input-proxy"
    "ExecStart=/usr/bin/input-proxy run @/etc/input-proxy/instances/%i.args"
    "Restart=on-failure"
    "[Install]"
    "WantedBy=multi-user.target"
)
    string(FIND "${template}" "${required_line}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR
            "Systemd template is missing required line: ${required_line}")
    endif()
endforeach()

string(REGEX MATCHALL "ExecStart=" exec_start_lines "${template}")
list(LENGTH exec_start_lines exec_start_count)
if(NOT exec_start_count EQUAL 1)
    message(FATAL_ERROR
        "Systemd template must contain exactly one ExecStart directive")
endif()

if(template MATCHES "--(source|name|activity-timeout-ms|detection-throttle-ms|running-motion-activity|paused-motion-activity|start-paused)")
    message(FATAL_ERROR
        "Systemd template must not duplicate response-artifact runtime policy")
endif()
