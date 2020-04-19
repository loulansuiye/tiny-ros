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

int main(void) {
  //{ init lwip
  lwip_sys_init();
  // }

  tinyros::ServiceClient<tinyros_hello::Test::Request, tinyros_hello::Test::Response> client("test_srv");
  tinyros::nh()->serviceClient(client);
  while (true) {
    tinyros_hello::Test::Request req;
    tinyros_hello::Test::Response res;
    req.input = "hello world!";
    if (client.call(req, res)) {
       printf("Service responsed with \"%s\"\n", res.output.c_str());
    } else {
       printf("Service call failed.\n");
    }
    rt_thread_delay(1000);
  }

  return 0;
}
