/*
 * File      : node_handle.h
 * This file is part of tiny_ros
 *
 * Change Logs:
 * Date           Author       Notes
 * 2018-04-24     Pinkie.Fu    initial version
 */

#ifndef TINYROS_NODE_HANDLE_H_
#define TINYROS_NODE_HANDLE_H_
#include <stdint.h>
#include <math.h>
#include <queue>
#include <rtthread.h>
#include "tiny_ros/ros/time.h"
#include "tiny_ros/ros/threadpool.h"
#include "tiny_ros/roslib_msgs/Time.h"
#include "tiny_ros/roslib_msgs/String.h"
#include "tiny_ros/tinyros_msgs/TopicInfo.h"
#include "tiny_ros/tinyros_msgs/Log.h"

#define TINYROS_LOG_TOPIC "tinyros_log_11315"
namespace tinyros {
class SpinObject {
public:
  int id;
  uint8_t *message_in;
  SpinObject() { message_in = NULL; }
  ~SpinObject() { if(message_in) free((void*)message_in); }
};

class NodeHandleBase_
{
public:
  virtual int publish(int id, const Msg* msg, bool islog = false) = 0;
  virtual int spin() = 0;
  virtual void exit() = 0;
  virtual bool ok() = 0;
};
}

#include "tiny_ros/ros/msg.h"
#include "tiny_ros/ros/publisher.h"
#include "tiny_ros/ros/subscriber.h"
#include "tiny_ros/ros/service_server.h"
#include "tiny_ros/ros/service_client.h"
#include "tiny_ros/ros/hardware.h"

namespace tinyros
{
const int SPIN_OK = 0;
const int SPIN_ERR = -1;

const uint8_t MODE_FIRST_FF = 0;

const uint8_t MODE_PROTOCOL_VER   = 1;
const uint8_t PROTOCOL_VER        = 0xb9;
const uint8_t MODE_SIZE_L         = 2;
const uint8_t MODE_SIZE_L1        = 3;
const uint8_t MODE_SIZE_H         = 4;
const uint8_t MODE_SIZE_H1        = 5;
const uint8_t MODE_SIZE_CHECKSUM  = 6;    // checksum for msg size received from size L and H
const uint8_t MODE_TOPIC_L        = 7;    // waiting for topic id
const uint8_t MODE_TOPIC_H        = 8;
const uint8_t MODE_MESSAGE        = 9;
const uint8_t MODE_MSG_CHECKSUM   = 10;    // checksum for msg and topic id

const int MAX_SUBSCRIBERS = 10;
const int MAX_PUBLISHERS = 10;
const int INPUT_SIZE = 100; // bytes
const int OUTPUT_SIZE = 100; // bytes

using tinyros_msgs::TopicInfo;

class NodeHandle : public NodeHandleBase_
{
public:
  Hardware hardware_;

  Hardware loghd_;
  Publisher logpb_;

  struct rt_mutex mutex_;

  ThreadPool<NodeHandle> spin_thread_pool_;
  ThreadPool<NodeHandle> spin_log_thread_pool_;
  ThreadPool<NodeHandle> spin_srv_thread_pool_;

  uint8_t *message_in;
  uint8_t *message_tmp;
  uint8_t *message_out;

  Publisher * publishers[MAX_PUBLISHERS];
  Subscriber_ * subscribers[MAX_SUBSCRIBERS];

  std::string executable_name;
  uint32_t executable_len;

public:
  NodeHandle()
    : logpb_(TINYROS_LOG_TOPIC, new tinyros_msgs::Log)
    , spin_thread_pool_("spin", 2, 1024)
    , spin_log_thread_pool_("log", 1, 1024)
    , spin_srv_thread_pool_("srv", 2, 1024)
    , executable_name("")
    , executable_len(0)
    , topic_list("")
    , service_list("") {
    executable_name = "RTTHREAD";
    executable_len = executable_name.length();

    rt_mutex_init(&mutex_, "nh", RT_IPC_FLAG_FIFO);

    for (unsigned int i = 0; i < MAX_PUBLISHERS; i++)
      publishers[i] = NULL;

    for (unsigned int i = 0; i < MAX_SUBSCRIBERS; i++)
      subscribers[i] = NULL;

    message_in = (uint8_t*)calloc(INPUT_SIZE, sizeof(uint8_t));
    message_tmp = (uint8_t*)calloc(INPUT_SIZE, sizeof(uint8_t));
    message_out = (uint8_t*)calloc(OUTPUT_SIZE, sizeof(uint8_t));

    advertise(logpb_);
  }

  ~NodeHandle() {
    hardware_.close();
    loghd_.close();

    if (message_in) free((void*)message_in);
    if (message_tmp) free((void*)message_tmp);
    if (message_out) free((void*)message_out);

    rt_mutex_detach(&mutex_);
  }

  Hardware* getHardware()
  {
    return &hardware_;
  }

  /* Start a named port, which may be network server IP, initialize buffers */
  bool initNode(std::string portName = "127.0.0.1") {
    bytes_ = 0;
    index_ = 0;
    topic_ = 0;
    spin_ = true;
    mode_ = MODE_FIRST_FF;

    roslib_msgs::String msg;
    if (hardware_.init(portName)) {
      msg.data = executable_name;
      publish(TopicInfo::ID_SESSION_ID, &msg);
    }
    if (loghd_.init(portName)) {
      msg.data = executable_name + "_log";
      publish(TopicInfo::ID_SESSION_ID, &msg, true);
    }
    return (hardware_.connected() && loghd_.connected());
  }

  virtual void exit() {
    spin_ = false;
    loghd_.close();
    hardware_.close();
  }

protected:
  //State machine variables for spin
  int mode_;
  int bytes_;
  int topic_;
  int index_;
  int checksum_;
  bool spin_;
  int total_bytes_;

public:
  /* This function goes in your loop() function, it handles
   *  serial input and callbacks for subscribers.
   */
  virtual int spin() {
    int i, rv, len = 1;

    mode_ = MODE_FIRST_FF;

    rt_mutex_take(&mutex_, RT_WAITING_FOREVER);
    for (unsigned int i = 0; i < MAX_PUBLISHERS; i++) {
      if (publishers[i] != NULL)
        publishers[i]->negotiated_ = false;
    }
    for (unsigned int i = 0; i < MAX_SUBSCRIBERS; i++) {
      if (subscribers[i] != NULL)
        subscribers[i]->negotiated_ = false;
    }
    rt_mutex_release(&mutex_);

    if (!hardware_.connected()) {
      return SPIN_ERR;
    } else {
      negotiateTopics();
    }

    /* while available buffer, read data */
    while (spin_ && hardware_.connected()) {
      if (len > INPUT_SIZE) {
        len = 1;
        mode_ = MODE_FIRST_FF;
        continue;
      }

      rv = hardware_.read(message_tmp, len);
      if (rv < 0) {
        mode_ = MODE_FIRST_FF;
        return SPIN_ERR;
      }

      for (i = 0; i < rv; i++) {
        checksum_ += message_tmp[i];
      }

      if (mode_ == MODE_MESSAGE) {         /* message data being recieved */
        for (i = 0; i < rv; i++) {
          message_in[index_++] = message_tmp[i];
          bytes_--;
        }

        if (bytes_ == 0) {                /* is message complete? if so, checksum */
          len = 1;
          mode_ = MODE_MSG_CHECKSUM;
        } else {
          len = bytes_;
        }
      } else if (mode_ == MODE_FIRST_FF) {
        if (message_tmp[0] == 0xff) {
          mode_++;
        }
      } else if (mode_ == MODE_PROTOCOL_VER) {
        if (message_tmp[0] == PROTOCOL_VER) {
          mode_++;
        } else {
          mode_ = MODE_FIRST_FF;
        }
      } else if (mode_ == MODE_SIZE_L) {     /* bottom half of message size */
        bytes_ = message_tmp[0];
        index_ = 0;
        mode_++;
        checksum_ = message_tmp[0];
      } else if (mode_ == MODE_SIZE_L1) {
        bytes_ += message_tmp[0] << 8;
        mode_++;
      } else if (mode_ == MODE_SIZE_H) {     /* top half of message size */
        bytes_ += message_tmp[0] << 16;
        mode_++;
      } else if (mode_ == MODE_SIZE_H1) {
        bytes_ += message_tmp[0] << 24;
        total_bytes_ = bytes_ > 0 ? bytes_ : 1;
        mode_++;
      } else if (mode_ == MODE_SIZE_CHECKSUM) {
        if ((checksum_ % 256) == 255)
          mode_++;
        else
          mode_ = MODE_FIRST_FF;          /* Abandon the frame if the msg len is wrong */
      } else if (mode_ == MODE_TOPIC_L) {    /* bottom half of topic id */
        topic_ = message_tmp[0];
        mode_++;
        checksum_ = message_tmp[0];               /* first byte included in checksum */
      } else if (mode_ == MODE_TOPIC_H) {     /* top half of topic id */
        topic_ += message_tmp[0] << 8;
        mode_ = MODE_MESSAGE;
        if (bytes_ == 0)
          mode_ = MODE_MSG_CHECKSUM;
        else
          len = bytes_;
      } else if (mode_ == MODE_MSG_CHECKSUM) {    /* do checksum */
        mode_ = MODE_FIRST_FF;
        if ((checksum_ % 256) == 255) {
          if (topic_ == TopicInfo::ID_PUBLISHER) {
            negotiateTopics();
          } else if (topic_ == TopicInfo::ID_ROSTOPIC_REQUEST) {
            roslib_msgs::String msg;
            msg.deserialize(message_in);
            topic_list = msg.data;
            topic_list_recieved = true;
          } else if (topic_ == TopicInfo::ID_ROSSERVICE_REQUEST) {
            roslib_msgs::String msg;
            msg.deserialize(message_in);
            service_list = msg.data;
            service_list_recieved = true;
          } else if (topic_ == TopicInfo::ID_NEGOTIATED) {
            tinyros_msgs::TopicInfo ti;
            ti.deserialize(message_in);
            for (int i = 0; i < MAX_PUBLISHERS; i++) {
              if (publishers[i] != NULL && publishers[i]->id_ == ti.topic_id) {
                publishers[i]->negotiated_ = ti.negotiated;
              }
            }

            for (int i = 0; i < MAX_SUBSCRIBERS; i++) {
              if (subscribers[i] != NULL && subscribers[i]->id_ == ti.topic_id) {
                subscribers[i]->negotiated_ = ti.negotiated;
              }
            }
          } else {
            if (((topic_-100) >= 0) && subscribers[topic_-100]) {
              if(subscribers[topic_-100]) {
                SpinObject* obj = new SpinObject();
                obj->id = topic_-100;
                obj->message_in = (uint8_t*)calloc(total_bytes_, sizeof(uint8_t));
                memcpy(obj->message_in, message_in, total_bytes_);
                if (!strcmp(subscribers[topic_-100]->topic_, TINYROS_LOG_TOPIC)) {
                  spin_log_thread_pool_.schedule(&NodeHandle::spin_task, this, obj);
                } else {
                  if (subscribers[topic_-100]->srv_flag_) {
                    spin_srv_thread_pool_.schedule(&NodeHandle::spin_task, this, obj);
                  } else {
                    spin_thread_pool_.schedule(&NodeHandle::spin_task, this, obj);
                  }
                }
              }
            }
          }
        }
      }
    }

    return SPIN_OK;
  }

  void spin_task(void *arg, bool destroy) {
    SpinObject* obj = (SpinObject*) arg;
    if (!obj) return;
    if (!destroy) {
      if(subscribers[obj->id] && obj->message_in){
        int64_t time_start = (int64_t)tinyros::Time().now().toMSec();
        int64_t timeout_time = time_start + 1000;

        subscribers[obj->id]->callback( obj->message_in );

        int64_t time_end = (int64_t)tinyros::Time().now().toMSec();
        if (time_end > timeout_time) {
          char bufer[512];
          snprintf(bufer, sizeof(bufer), "[%s] subscriber topic: %s, time escape: %lld(ms)", executable_name.c_str(),
              subscribers[obj->id]->topic_, (time_end - time_start));
          logwarn(bufer);
        }
      }
    }

    delete obj;
  }

  /* Are we connected to the PC? */
  virtual bool ok() {
    return (hardware_.connected() && loghd_.connected());
  }

  /********************************************************************
   * Topic Management
   */

  /* Register a new publisher */
  bool advertise(Publisher & p) {
    char buffer[512];
    rt_mutex_take(&mutex_, RT_WAITING_FOREVER);
    for (int i = 0; i < MAX_PUBLISHERS; i++) {
      if (publishers[i] == NULL) { // empty slot
        p.id_ = i + 100 + MAX_SUBSCRIBERS;
        p.nh_ = this;
        publishers[i] = &p;
        rt_mutex_release(&mutex_);

        negotiateTopics(publishers[i]);
        snprintf(buffer, sizeof(buffer), "[%s] Publishers[%d] topic_id: %d, topic_name: %s", executable_name.c_str(), i, p.id_, p.topic_);
        logdebug(buffer);
        return true;
      }
    }
    rt_mutex_release(&mutex_);
    return false;
  }

  /* Register a new subscriber */
  template<typename SubscriberT>
  bool subscribe(SubscriberT& s) {
    char buffer[512];
    rt_mutex_take(&mutex_, RT_WAITING_FOREVER);
    for (int i = 0; i < MAX_SUBSCRIBERS; i++) {
      if (subscribers[i] == 0) {// empty slot
        s.id_ = i + 100;
        subscribers[i] = &s;
        rt_mutex_release(&mutex_);

        negotiateTopics(subscribers[i]);
        snprintf(buffer, sizeof(buffer), "[%s] Subscribers[%d] topic_id: %d, topic_name: %s", executable_name.c_str(), i, s.id_, s.topic_);
        logdebug(buffer);
        return true;
      }
    }
    rt_mutex_release(&mutex_);
    return false;
  }

  /* Register a new Service Server */
  template<typename MReq, typename MRes, typename ObjT>
  bool advertiseService(ServiceServer<MReq, MRes, ObjT>& srv) {
    char buffer[512];
    rt_mutex_take(&mutex_, RT_WAITING_FOREVER);
    for (int i = 0; i < MAX_SUBSCRIBERS; i++) {
      if (subscribers[i] == NULL) { // empty slot
        subscribers[i] = &srv;
        srv.id_ = i + 100;
        for (int j = 0; j < MAX_PUBLISHERS; j++) {
          if (publishers[j] == NULL) { // empty slot
            publishers[j] = &srv.pub;
            srv.pub.id_ = srv.id_;
            srv.pub.nh_ = this;
            rt_mutex_release(&mutex_);

            negotiateTopics(subscribers[i]);
            negotiateTopics(publishers[j]);
            snprintf(buffer, sizeof(buffer), "[%s] advertiseService Subscribers[%d] topic_id: %d, topic_name: %s | "
                "Publishers[%d] topic_id: %d, topic_name: %s", executable_name.c_str(), i, srv.id_, srv.topic_, j, srv.pub.id_, srv.pub.topic_);
            logdebug(buffer);
            return true;
          }
        }
      }
    }
    rt_mutex_release(&mutex_);
    return false;
  }

  /* Register a new Service Client */
  template<typename MReq, typename MRes>
  bool serviceClient(ServiceClient<MReq, MRes>& srv) {
    char buffer[512];
    rt_mutex_take(&mutex_, RT_WAITING_FOREVER);
    for (int i = 0; i < MAX_SUBSCRIBERS; i++) {
      if (subscribers[i] == NULL) { // empty slot
        subscribers[i] = &srv;
        srv.id_ = i + 100;
        for (int j = 0; j < MAX_PUBLISHERS; j++) {
          if (publishers[j] == NULL) { // empty slot
            publishers[j] = &srv.pub;
            srv.pub.id_ = srv.id_;
            srv.pub.nh_ = this;
            rt_mutex_release(&mutex_);

            negotiateTopics(subscribers[i]);
            negotiateTopics(publishers[j]);
            snprintf(buffer, sizeof(buffer), "[%s] serviceClient Subscribers[%d] topic_id: %d, topic_name: %s | "
                "Publishers[%d] topic_id: %d, topic_name: %s", executable_name.c_str(), i, srv.id_, srv.topic_, j, srv.pub.id_, srv.pub.topic_);
            logdebug(buffer);
            return true;
          }
        }
      }
    }
    rt_mutex_release(&mutex_);
    return false;
  }

  void negotiateTopics(Publisher * p) {
    tinyros_msgs::TopicInfo ti;
    ti.topic_id = p->id_;
    ti.topic_name = (char *) p->topic_;
    ti.message_type = (char *) p->msg_->getType();
    ti.md5sum = (char *) p->msg_->getMD5();
    ti.buffer_size = OUTPUT_SIZE;
    if (p != &logpb_) {
      publish(p->getEndpointType(), &ti);
    } else {
      publish(p->getEndpointType(), &ti, true);
    }
  }

  void negotiateTopics(Subscriber_ * s) {
    tinyros_msgs::TopicInfo ti;
    ti.topic_id = s->id_;
    ti.topic_name = (char *) s->topic_;
    ti.message_type = (char *) s->getMsgType();
    ti.md5sum = (char *) s->getMsgMD5();
    ti.buffer_size = INPUT_SIZE;
    publish(s->getEndpointType(), &ti);
  }

  void negotiateTopics() {
    int i;
    for (i = 0; i < MAX_PUBLISHERS; i++) {
      rt_mutex_take(&mutex_, RT_WAITING_FOREVER);
      if (publishers[i] != NULL) { // non-empty slot
        rt_mutex_release(&mutex_);
        negotiateTopics(publishers[i]);
      } else {
        rt_mutex_release(&mutex_);
      }
    }
    for (i = 0; i < MAX_SUBSCRIBERS; i++) {
      rt_mutex_take(&mutex_, RT_WAITING_FOREVER);
      if (subscribers[i] != NULL) { // non-empty slot
        rt_mutex_release(&mutex_);
        negotiateTopics(subscribers[i]);
      } else {
        rt_mutex_release(&mutex_);
      }
    }
  }

  virtual int publish(int id, const Msg * msg, bool islog = false) {
    rt_mutex_take(&mutex_, RT_WAITING_FOREVER);
    /* serialize message */
    int l = msg->serialize(message_out + 9);

    /* setup the header */
    message_out[0] = 0xff;
    message_out[1] = PROTOCOL_VER;
    message_out[2] = (uint8_t)((uint32_t)l & 0xFF);
    message_out[3] = (uint8_t)((uint32_t)((l >> 8) & 0xFF));
    message_out[4] = (uint8_t)((uint32_t)((l >> 16) & 0xFF));
    message_out[5] = (uint8_t)((uint32_t)((l >> 24) & 0xFF));
    message_out[6] = 255 - ((message_out[2] + message_out[3] + message_out[4] + message_out[5]) % 256);
    message_out[7] = (uint8_t)((int16_t)id & 255);
    message_out[8] = (uint8_t)((int16_t)id >> 8);

    /* calculate checksum */
    int chk = 0;
    for (int i = 7; i < l + 9; i++)
      chk += message_out[i];
    l += 9;
    message_out[l++] = 255 - (chk % 256);

    if (l <= OUTPUT_SIZE) {
      if (!islog) {
        l = hardware_.write(message_out, l) ? l : -1;
      } else {
        l = loghd_.write(message_out, l) ? l : -1;
      }
      rt_mutex_release(&mutex_);
      if (l < 0)
        logerror("Message from device dropped: message write error.");
      return l;
    } else {
      rt_mutex_release(&mutex_);
      logerror("Message from device dropped: message larger than buffer.");
      return -2;
    }
  }

  /********************************************************************
   * Logging
   */

private:
  void log(char byte, std::string msg)
  {
    if (loghd_.connected()) 
    {
      tinyros_msgs::Log l;
      l.level = byte;
      l.msg = msg;
      logpb_.publish(&l, true);
    }
  }

public:
  void logdebug(std::string msg) { log(tinyros_msgs::Log::ROSDEBUG, msg); }
  void loginfo(std::string msg) { log(tinyros_msgs::Log::ROSINFO, msg); }
  void logwarn(std::string msg) { log(tinyros_msgs::Log::ROSWARN, msg); }
  void logerror(std::string msg) { log(tinyros_msgs::Log::ROSERROR, msg); }
  void logfatal(std::string msg) { log(tinyros_msgs::Log::ROSFATAL, msg); }

  /*********************************************************************/
private:
  bool topic_list_recieved;
  std::string topic_list;

  bool service_list_recieved;
  std::string service_list;

public:
  std::string getTopicList(int timeout = 1000)
  {
    roslib_msgs::String msg;
    publish(TopicInfo::ID_ROSTOPIC_REQUEST, &msg);
    int64_t to = (int64_t)(tinyros::Time().now().toMSec() + timeout);
    topic_list_recieved = false;
    while (!topic_list_recieved)
    {
      int64_t now = (int64_t)tinyros::Time().now().toMSec();
      if (now > to )
      {
        logwarn("Failed to get getTopicList: timeout expired");
        return "";
      }
    }
    return topic_list;
  }

  std::string getServiceList(int timeout = 1000)
  {
    roslib_msgs::String msg;
    publish(TopicInfo::ID_ROSSERVICE_REQUEST, &msg);
    int64_t to = (int64_t)(tinyros::Time().now().toMSec() + timeout);
    service_list_recieved = false;
    while (!service_list_recieved)
    {
      int64_t now = (int64_t)tinyros::Time().now().toMSec();
      if (now > to)
      {
        logwarn("Failed to get getServiceList: timeout expired");
        return "";
      }
    }
    return service_list;
  }
};

NodeHandle* nh();

}

#endif

