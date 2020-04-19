/*
 * File      : ExampleService.cpp
 * This file is part of tinyros
 * Copyright (C) UBTECH Robotics 2018
 *
 * Change Logs:
 * Date           Author       Notes
 * 2018-01-22     Pinkie.Fu    initial version
 */
#include <rtthread.h>
#include <lwip/sys.h>
#include <lwip/api.h>
#include <netif/ethernetif.h>
#include "tiny_ros/ros.h"
#include "tiny_ros/tinyros_hello/Test.h"

extern "C" void lwip_sys_init(void);

static void service_cb(const tinyros_hello::Test::Request & req,
    tinyros_hello::Test::Response & res) {
  res.output = "Hello, tiny-ros ^_^";
}

int main(void) {
  //{ init lwip
  lwip_sys_init();
  // }

  tinyros::ServiceServer<tinyros_hello::Test::Request,
    tinyros_hello::Test::Response> server("test_srv", &service_cb);
  tinyros::nh()->advertiseService(server);
  while(true) {
    rt_thread_delay(10*1000);
  }
  return 0;
}
