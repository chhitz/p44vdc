//
//  Copyright (c) 2013-2016 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  Author: Lukas Zeller <luz@plan44.ch>
//
//  This file is part of vdcd.
//
//  vdcd is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  vdcd is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with vdcd. If not, see <http://www.gnu.org/licenses/>.
//

#include "application.hpp"

#include "jsonrpccomm.hpp"

#define DEFAULT_VDSMSERVICE "8440"
#define MAINLOOP_CYCLE_TIME_uS 20000 // 20mS
#define DEFAULT_LOGLEVEL LOG_NOTICE


using namespace p44;

class JsonRpcTool : public Application
{
  JsonRpcCommPtr jsonRpcComm; // current connection
  FdCommPtr userInput;

  SocketCommPtr jsonRpcServer; // server waiting for connection

  typedef enum {
    idle,
    waiting_for_method,
    waiting_for_notification,
    waiting_for_params,
    waiting_for_errorcode,
    waiting_for_result
  } InputState;


  InputState inputState;
  bool sendNotification;
  string method;
  string lastId;
  bool autoaccept;

public:

  JsonRpcTool() :
    inputState(idle),
    autoaccept(false)
  {
    userInput = FdCommPtr(new FdComm(MainLoop::currentMainLoop()));
    jsonRpcServer = SocketCommPtr(new SocketComm(MainLoop::currentMainLoop()));
  }


  void usage(char *name)
  {
    fprintf(stderr, "usage:\n");
    fprintf(stderr, "  %s [options]\n", name);
    fprintf(stderr, "    -c jsonrpchost  : host for making connection to\n");
    fprintf(stderr, "    -C jsonrpcport  : port number/service name for JSON service (default=%s)\n", DEFAULT_VDSMSERVICE);
    fprintf(stderr, "    -a              : auto-respond to 'announce' and 'announcevdc' method calls from vDC\n");
    fprintf(stderr, "    -l loglevel     : set loglevel (default = %d)\n", DEFAULT_LOGLEVEL);
  };

  virtual int main(int argc, char **argv)
  {
    if (argc<2) {
      // show usage
      usage(argv[0]);
      exit(1);
    }

    int loglevel = DEFAULT_LOGLEVEL; // use defaults

    char *jsonrpchost = NULL;
    char *jsonrpcport = (char *) DEFAULT_VDSMSERVICE;

    int c;
    while ((c = getopt(argc, argv, "C:c:l:a")) != -1)
    {
      switch (c) {
        case 'c':
          jsonrpchost = optarg;
          break;
        case 'C':
          jsonrpcport = optarg;
          break;
        case 'l':
          loglevel = atoi(optarg);
          break;
        case 'a':
          autoaccept = true;
          break;
        default:
          exit(-1);
      }
    }

    SETLOGLEVEL(loglevel);

    // Create JSON RPC client or server connection
    if (jsonrpchost) {
      // connect as client to a server
      jsonRpcComm = JsonRpcCommPtr(new JsonRpcComm(MainLoop::currentMainLoop()));
      jsonRpcComm->setConnectionParams(jsonrpchost, jsonrpcport, SOCK_STREAM, AF_INET);
      jsonRpcComm->setConnectionStatusHandler(boost::bind(&JsonRpcTool::jsonRpcClientConnectionHandler, this, _2));
      jsonRpcComm->setRequestHandler(boost::bind(&JsonRpcTool::jsonRpcRequestHandler, this, _1, _2, _3));
      jsonRpcComm->initiateConnection();

    }
    else {
      // be server
      jsonRpcServer->setConnectionParams(NULL, jsonrpcport, SOCK_STREAM, AF_INET);
      jsonRpcServer->setAllowNonlocalConnections(true);
      jsonRpcServer->startServer(boost::bind(&JsonRpcTool::jsonRpcServerConnectionHandler, this, _1), 1);
    }

    // init user input
    userInput->setReceiveHandler(boost::bind(&JsonRpcTool::userInputHandler, this, _1), '\n');
    userInput->setFd(STDIN_FILENO);
    userInput->makeNonBlocking();

    // app now ready to run
    return run();
  }



  SocketCommPtr jsonRpcServerConnectionHandler(SocketCommPtr aServerSocketComm)
  {
    printf("++++++++++++++ Connection from server\n");
    jsonRpcComm = JsonRpcCommPtr(new JsonRpcComm(MainLoop::currentMainLoop()));
    jsonRpcComm->setReportAllErrors(true); // server should report all errors
    jsonRpcComm->setRequestHandler(boost::bind(&JsonRpcTool::jsonRpcRequestHandler, this, _1, _2, _3));
    askMethod();
    return jsonRpcComm;
  }


  void jsonRpcClientConnectionHandler(ErrorPtr aError)
  {
    if (Error::isOK(aError)) {
      // connection successfully opened
      printf("\nConnection established\n");
      askMethod();
    }
    else {
      // error on vdSM connection, was closed
      printf("\nConnection terminated: %s\n\n", aError->description().c_str());
      // done
      exit(0);
    }
  }


  void jsonRpcRequestHandler(const char *aMethod, const char *aJsonRpcId, JsonObjectPtr aParams)
  {
    printf("\nJSON-RPC request id='%s', method='%s', params=%s\n\n", aJsonRpcId ? aJsonRpcId : "<none>", aMethod, aParams ? aParams->c_strValue() : "<none>");
    if (aJsonRpcId) {
      // this is a method call, expects answer
      if ((strcmp(aMethod,"announcedevice")==0 || strcmp(aMethod,"announcevdc")==0) && autoaccept) {
        // just send NULL result
        printf("Auto-responding with success to 'announcevdc and announcedevice' methods\n\n");
        jsonRpcComm->sendResult(aJsonRpcId, JsonObjectPtr());
      }
      else {
        lastId = aJsonRpcId; // save
        askErrorCode();
      }
    }
    else {
      askMethod();
    }
  }


  void jsonRpcResponseHandler(int32_t aResponseId, ErrorPtr &aError, JsonObjectPtr aResultOrErrorData)
  {
    if (Error::isOK(aError)) {
      printf("\nJSON-RPC result id=%d, result=%s\n", aResponseId, aResultOrErrorData ? aResultOrErrorData->c_strValue() : "NULL");
    }
    else {
      printf(
        "\nJSON-RPC error id=%d, code=%ld, message=%s, data=%s\n\n",
        aResponseId,
        aError->getErrorCode(),
        aError->description().c_str(),
        aResultOrErrorData ? aResultOrErrorData->c_strValue() : "<none>"
      );
    }
  }


  void inputPrompt()
  {
    switch (inputState) {
      case waiting_for_method:
      default:
        printf("\nEnter method name (or just enter to create notification) : "); break;
      case waiting_for_notification: printf("Enter notification name: "); break;
      case waiting_for_params: printf("Enter params JSON (just enter for no params) : "); break;
      case waiting_for_errorcode: printf("\nEnter error code (or nothing to send result) : "); break;
      case waiting_for_result: printf("Enter result JSON (or nothing for NULL result) : "); break;
    }
    fflush(stdout);
  }


  void askMethod()
  {
    inputState = waiting_for_method;
    inputPrompt();
  }

  void askNotification()
  {
    inputState = waiting_for_notification;
    inputPrompt();
  }

  void askParams()
  {
    inputState = waiting_for_params;
    inputPrompt();
  }

  void askErrorCode()
  {
    inputState = waiting_for_errorcode;
    inputPrompt();
  }

  void askResult()
  {
    inputState = waiting_for_result;
    inputPrompt();
  }


  void userInputHandler(ErrorPtr aError)
  {
    // get user input
    string text;
    userInput->receiveDelimitedString(text);
    //printf("User input = %s\n", jsonText.c_str());
    if (inputState==waiting_for_method) {
      if (text.size()>0) {
        sendNotification = false;
        method = text;
        askParams();
      }
      else {
        askNotification();
      }
    }
    else if (inputState==waiting_for_notification) {
      method = text;
      sendNotification = true;
      askParams();
    }
    else if (inputState==waiting_for_params) {
      // JSON params, try to parse
      JsonObjectPtr params;
      if (text.size()>0) {
        params = JsonObject::objFromText(text.c_str());
        if (!params || !params->isType(json_type_object)) {
          printf("Invalid params JSON - must be JSON object, please re-enter\n");
          inputPrompt();
          return;
        }
      }
      else {
        // empty input means no params
        params = JsonObjectPtr();
      }
      // ok, launch request
      if (sendNotification)
        jsonRpcComm->sendRequest(method.c_str(), params); // no answer expected
      else
        jsonRpcComm->sendRequest(method.c_str(), params, boost::bind(&JsonRpcTool::jsonRpcResponseHandler, this, _1, _2, _3)); // answer expected, add handler
      // and ask for next method
      inputState = waiting_for_method;
      MainLoop::currentMainLoop().executeOnce(boost::bind(&JsonRpcTool::inputPrompt,this), 200*MilliSecond);
    }
    else if (inputState==waiting_for_errorcode) {
      if (text.size()>0) {
        // error code entered
        int32_t errCode = 0;
        if (sscanf(text.c_str(), "%d", &errCode)==1) {
          // send back error
          jsonRpcComm->sendError(lastId.c_str(), errCode);
        }
        else {
          printf("Invalid error code - must be decimal integer, please re-enter\n");
          inputPrompt();
          return;
        }
      }
      else {
        // no error code, means we want to answer
        askResult();
        return;
      }
    }
    else if (inputState==waiting_for_result) {
      // JSON result, try to parse
      JsonObjectPtr result;
      if (text.size()>0) {
        result = JsonObject::objFromText(text.c_str());
        if (!result) {
          printf("Invalid result JSON, please re-enter\n");
          inputPrompt();
          return;
        }
      }
      else {
        // empty input means NULL result
        result = JsonObjectPtr();
      }
      // ok, send result
      jsonRpcComm->sendResult(lastId.c_str(), result);
      // and ask for next method
      inputState = waiting_for_method;
      MainLoop::currentMainLoop().executeOnce(boost::bind(&JsonRpcTool::inputPrompt,this), 200*MilliSecond);
    }
    else {
      // invalid
      printf("Invalid input\n\n");
      askMethod();
    }
  }


  virtual void initialize()
  {
  }


};


int main(int argc, char **argv)
{
  // create the mainloop
  MainLoop::currentMainLoop().setLoopCycleTime(MAINLOOP_CYCLE_TIME_uS);
  // create app with current mainloop
  static JsonRpcTool application;
  // pass control
  return application.main(argc, argv);
}
