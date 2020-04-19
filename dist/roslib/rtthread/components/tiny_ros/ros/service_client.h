/*
 * File      : service_client.h
 * This file is part of tiny_ros
 *
 * Change Logs:
 * Date           Author       Notes
 * 2018-04-24     Pinkie.Fu    initial version
 */

#ifndef _TINYROS_SERVICE_CLIENT_H_
#define _TINYROS_SERVICE_CLIENT_H_
#include <stdint.h>
#include <rtthread.h>
#include "tiny_ros/tinyros_msgs/TopicInfo.h"
#include "tiny_ros/ros/publisher.h"
#include "tiny_ros/ros/subscriber.h"

namespace tinyros
{

template<typename MReq , typename MRes>
class ServiceClient : public Subscriber_
{
public:
  ServiceClient(const char* topic_name) :
    pub(topic_name, &req, tinyros_msgs::TopicInfo::ID_SERVICE_CLIENT + tinyros_msgs::TopicInfo::ID_PUBLISHER)
  {
    this->negotiated_ = false;
    this->srv_flag_ = true;
    this->topic_ = topic_name;
    this->call_resp = NULL;
    this->call_req = NULL;
    this->waiting = true;
    rt_mutex_init(&mutex_, "sc", RT_IPC_FLAG_FIFO);
    rt_mutex_init(&g_mutex_, "gsc", RT_IPC_FLAG_FIFO);
  }
  virtual bool call(MReq & request, MRes & response, int duration = 3)
  {
    bool ret = false;
    rt_mutex_take(&g_mutex_, RT_WAITING_FOREVER);
    rt_mutex_take(&mutex_, RT_WAITING_FOREVER);
    do {
      if (!pub.nh_->ok()) { break; }
      call_req = &request;
      call_resp = &response;
      
      rt_mutex_take(gg_mutex_, RT_WAITING_FOREVER);
      call_req->setID(gg_id_++);
      rt_mutex_release(gg_mutex_);
      
      if (pub.publish(&request) <= 0) { break; }
      this->waiting = true;
      rt_mutex_release(&mutex_);
      
      int count = (duration * 1000) / 50;
      while (this->waiting && count >= 0) {
        if (!this->waiting) { ret = true; break; }
        if (count == 0) { break; }
        rt_thread_delay(50);
        count--;
      }
      if (!ret) {
        printf("Service[%s] call_req.id: %u, call timeout", this->topic_, call_req->getID());
      }
      rt_mutex_take(&mutex_, RT_WAITING_FOREVER);
    } while(0);
    
    call_req = NULL; call_resp = NULL;
    rt_mutex_release(&g_mutex_);
    return ret;
  }

  // these refer to the subscriber
  virtual void callback(unsigned char *data)
  {
    if (call_resp && call_req) {
      rt_mutex_take(&mutex_, RT_WAITING_FOREVER);
      if (call_resp && call_req) {
        uint32_t req_id  = call_req->getID();
        uint32_t resp_id =  ((uint32_t) (*(data + 0)));
        resp_id |= ((uint32_t) (*(data + 1))) << (8 * 1);
        resp_id |= ((uint32_t) (*(data + 2))) << (8 * 2);
        resp_id |= ((uint32_t) (*(data + 3))) << (8 * 3);

        if (req_id == resp_id) {
          call_resp->deserialize(data);
          this->waiting = false;
        }
      }
      rt_mutex_release(&mutex_);
    }
  }
  virtual const char * getMsgType()
  {
    return this->resp.getType();
  }
  virtual const char * getMsgMD5()
  {
    return this->resp.getMD5();
  }
  virtual int getEndpointType()
  {
    return tinyros_msgs::TopicInfo::ID_SERVICE_CLIENT + tinyros_msgs::TopicInfo::ID_SUBSCRIBER;
  }
  
  virtual bool negotiated()
  { 
    return (negotiated_ && pub.negotiated_); 
  }

  ~ServiceClient() {
    rt_mutex_delete(gg_mutex_);
  }

  MReq req;
  MRes resp;
  MReq * call_req;
  MRes * call_resp;
  bool waiting;
  Publisher pub;
  struct rt_mutex mutex_;
  struct rt_mutex g_mutex_;
  static uint32_t gg_id_;
  static rt_mutex_t gg_mutex_;
};

template<typename MReq , typename MRes>
uint32_t ServiceClient<MReq , MRes>::gg_id_ = 1;

template<typename MReq , typename MRes>
  rt_mutex_t ServiceClient<MReq , MRes>::gg_mutex_ = rt_mutex_create("ggsc", RT_IPC_FLAG_FIFO);
}

#endif

