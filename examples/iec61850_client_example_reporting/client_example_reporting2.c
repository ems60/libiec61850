/*
* client_example_reporting.c
*
* This example is intended to be used with server_example_basic_io or server_example_goose.
*/

#include "iec61850_client.h"

#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <time.h>
#include <string>
#include "conversions.h"
#include <bitset>
#include <vector>
#include "hal_thread.h"

using namespace std;

static int running = 0;

void sigint_handler(int signalId)
{
    running = 0;
}

typedef struct _S61850ReportParam
{
    bool bUsed;     //是否启用，默认启用，如果数据集没有任何测点被使用就禁用掉
    std::string strIedName;
    std::string strLDName; //逻辑设备名，不包括IEDNAME部分
    std::string strDataSetDir;  //不带iedName MEAS/LLN0.dsAin
    std::string strDataSetRef;  //不带iedname MEAS/LLN0$dsAin
    std::string strRCB;         //不带iedname MEAS/LLN0.RP.brcbAin01
    std::string strRcbRef;      //不带iedname MEAS/LLN0.RP.brcbAin
    std::string strRealDataSetDir;  //带iedName EMUMEAS/LLN0.dsAin
    std::string strRealDataSetRef;  //带iedname EMUMEAS/LLN0$dsAin
    std::string strRealRCB;         //带iedname EMUMEAS/LLN0.RP.brcbAin01
    std::string strRealRcbRef;      //带iedname EMUMEAS/LLN0.RP.brcbAin
    uint32_t dwRCBMask;               //用来定义报告中包含哪些信息

    ClientReportControlBlock rcb;

    _S61850ReportParam() {
        bUsed = true;
        dwRCBMask = 0;
        rcb = NULL;
    }
}S61850ReportParam, *PS61850ReportParam;

//结构体类型mms，递归到第一个值类型
template<class T>
bool getValueFromMmsValue(MmsValue *curValue, const int nIdx, T &retValue)
{
    if (curValue == NULL)
    {
        return false;
    }

    MmsValue *tempValue = curValue;
    switch (MmsValue_getType(curValue)) {
    case MMS_STRUCTURE:
        if (nIdx >= static_cast<int>(MmsValue_getArraySize(tempValue)))  //超过范围
        {
            return false;
        }
        tempValue = MmsValue_getElement(tempValue, nIdx);
        return getValueFromMmsValue(tempValue, nIdx, retValue);
    case MMS_BOOLEAN:
        retValue = static_cast<T>(MmsValue_getBoolean(curValue));
        return true;
    case MMS_INTEGER: //同时包含32位和64位
        retValue = static_cast<T>(MmsValue_toInt64(curValue));
        return true;
    case MMS_UNSIGNED:
        retValue = static_cast<T>(MmsValue_toUint32(curValue));
        return true;
    case MMS_FLOAT://现在只解析了32位float，没有解析64位float
        retValue = static_cast<T>(MmsValue_toDouble(curValue));
        return true;
    case MMS_BIT_STRING:
        retValue = static_cast<T>(Quality_fromMmsValue(curValue));
        return true;
    case MMS_UTC_TIME:
        retValue = static_cast<T>(MmsValue_getUtcTimeInMs(curValue));
        return true;
    default:
        break;
    }

    return false;
}

bool isChangeReason(MmsValue *value)
{
    //ReasonForInclusion nReason = IEC61850_REASON_NOT_INCLUDED;
    if (value == NULL || MmsValue_getType(value) != MMS_BIT_STRING)
    {
        return false;
    }

    //上传原因:数据变化(1)|品质变化(2)|数据更新(3)|定时上报(4)|总召(5)
    if (MmsValue_getBitStringBit(value, 1))
    {
        return true;
    }

    return false;
}

void reportCallbackFunction(void* parameter, ClientReport clientReport)
{
    if (!running)//已收到系统退出命令，不处理数据。
        return;

    if (ClientReport_getRcbReference(clientReport) == NULL)
    {
        printf("received report but ClientReport_getRcbReference is NULL.\n");
    }
    else
    {
        printf("received report for %s\n", ClientReport_getRcbReference(clientReport));
    }

    if (ClientReport_getRptId(clientReport) == NULL)
    {
        printf("received report with rptId is NULL\n");
    }
    else
    {
        printf("received report with rptId %s\n", ClientReport_getRptId(clientReport));
    }

    if (ClientReport_getDataSetName(clientReport) == NULL)
    {
        printf("当前报告块中数据集名称为空.\n");
        return;
    }

    string strDataSetName = ClientReport_getDataSetName(clientReport); //数据集名称
    printf("received dataset %s\n", strDataSetName.c_str());

    MmsValue* dataSetValues = ClientReport_getDataValues(clientReport);
    MmsValue* inclusion = ClientReport_getInclusion(clientReport);
    if (dataSetValues == NULL || inclusion == NULL)
    {
        printf("received empty report for %s with rptId %s.\n", ClientReport_getRcbReference(clientReport),
            ClientReport_getRptId(clientReport));
        return;
    }

    if (!ClientReport_hasDataReference(clientReport))
    {
        printf("%s report has not dataReference,skip.\n", ClientReport_getRcbReference(clientReport));
        return;
    }

    int nValueIdx = ClientReport_getValueIndex(clientReport);  //值的起始地址,第1段是测点标识、第2段是值，第3段是原因
    int nElementNum = MmsValue_getNumberOfSetBits(inclusion);

    //校验一下总长度，防止越界
    int nTotalLen = MmsValue_getArraySize(dataSetValues);
    bool bHasReason = ClientReport_hasReasonForInclusion(clientReport);
    int nExpectMinLen = nValueIdx + nElementNum * (2 + static_cast<int>(bHasReason));
    if (nTotalLen < nExpectMinLen)
    {
        printf("%s report length error. Actual length=%d , expect length=%d.\n",
            ClientReport_getRcbReference(clientReport), nTotalLen, nExpectMinLen);
        return;
    }

    for (int i = 0; i < nElementNum; i++)
    {
        int nElementIndex = nValueIdx + i;
        MmsValue *curDSRef = MmsValue_getElement(dataSetValues, nElementIndex);
        if ((curDSRef == NULL) || (MmsValue_getType(curDSRef) != MMS_VISIBLE_STRING))
        {
            printf("IED_CLIENT: %s report contains invalid data reference,skip the report.\n", ClientReport_getRcbReference(clientReport));
            return;
        }

        string strDataRef = MmsValue_toString(curDSRef);
        MmsValue *curValue = MmsValue_getElement(dataSetValues, nElementIndex + nElementNum);
        MmsValue *curReason = NULL;
        if (bHasReason)
        {
            curReason = MmsValue_getElement(dataSetValues, nElementIndex + 2 * nElementNum);
        }

        printf("接收到数据: ref=%s", strDataRef.c_str());
        float fValue = 0.0;
        if (getValueFromMmsValue(curValue, 0, fValue))
        {
            printf(" 值=%f", fValue);
        }

        Quality nQulity = 0;
        if (getValueFromMmsValue(curValue, 1, nQulity))
        {
            printf(" 品质=%d", nQulity);
        }

        uint64_t lTime = 0L;
        if (getValueFromMmsValue(curValue, 2, lTime))
        {
            printf(" 时间戳=%lld", lTime);
        }
        printf("\n");
    }
}

void destoryReportResource(IedConnection con, S61850ReportParam &stParam)
{
    if (stParam.rcb != NULL)
    {
        IedConnection_uninstallReportHandler(con, stParam.strRealRcbRef.c_str());
        ClientReportControlBlock_destroy(stParam.rcb);
        stParam.rcb = NULL;
    }
}

int installOneReportCallBack(IedConnection con,S61850ReportParam &stReportParam)
{
    IedClientError error = IED_ERROR_UNKNOWN;
    ClientReportControlBlock &rcb = stReportParam.rcb;

    /* Read RCB values */
    rcb = IedConnection_getRCBValues(con, &error, stReportParam.strRealRCB.c_str(), NULL);
    if (error != IED_ERROR_OK || rcb == NULL)
    {
        printf("CIEC61850: IedConnection_getRCBValues失败,error=%d.\n", error);
        destoryReportResource(con,stReportParam);
        return -1;
    }

    /* prepare the parameters of the RCP */
    ClientReportControlBlock_setResv(rcb, true);
    ClientReportControlBlock_setTrgOps(rcb, TRG_OPT_DATA_CHANGED | TRG_OPT_QUALITY_CHANGED | TRG_OPT_GI);
    ClientReportControlBlock_setOptFlds(rcb, RPT_OPT_TIME_STAMP | RPT_OPT_REASON_FOR_INCLUSION | RPT_OPT_DATA_SET | RPT_OPT_DATA_REFERENCE | RPT_OPT_ENTRY_ID);
    ClientReportControlBlock_setDataSetReference(rcb, stReportParam.strRealDataSetRef.c_str()); /* NOTE the "$" instead of "." ! */
    //ClientReportControlBlock_setRptId(rcb, stReportParam.strRealRCB.c_str()); //正常不需要设置，但是有的厂商在报告块报文中使用的是报告块的实例号

    ClientReportControlBlock_setRptEna(rcb, true);
    //总召指令单独发送，所以此处就不赋值了
    //ClientReportControlBlock_setGI(rcb, true);

    /* Configure the report receiver */
    IedConnection_installReportHandler(con, stReportParam.strRealRcbRef.c_str(),
        ClientReportControlBlock_getRptId(rcb),reportCallbackFunction, NULL);


    /* Write RCB parameters and enable report */
    IedConnection_setRCBValues(con, &error, rcb, stReportParam.dwRCBMask, true);
    if (error != IED_ERROR_OK)
    {
        printf("CIEC61850: IedConnection_setRCBValues失败,rcb=%s error=%d\n", stReportParam.strRealRcbRef.c_str(), error);
        IedConnection_uninstallReportHandler(con, stReportParam.strRealRcbRef.c_str());
        destoryReportResource(con,stReportParam);
        return -1;
    }

    printf("CIEC61850:启用report成功.report=%s  real_report=%s\n",stReportParam.strRCB.c_str(), stReportParam.strRealRCB.c_str());

    return 0;
}

int enableOneReportGI(IedConnection con, S61850ReportParam &stReportParam)
{
    if (NULL == stReportParam.rcb)
    {
        return -1;
    }

    IedClientError error = IED_ERROR_OK;
    /* 总召 */
    ClientReportControlBlock_setGI(stReportParam.rcb, true);
    IedConnection_setRCBValues(con, &error, stReportParam.rcb, RCB_ELEMENT_GI, true);
    if (error != IED_ERROR_OK)
    {
        printf("CIEC61850:发送总召失败.dataset=%s  real_dataset=%s\n",
            stReportParam.strDataSetRef.c_str(), stReportParam.strRealDataSetRef.c_str());
    }
    else
    {
        printf("CIEC61850:发送总召成功.dataset=%s  real_dataset=%s\n",
            stReportParam.strDataSetRef.c_str(), stReportParam.strRealDataSetRef.c_str());
    }

    return 0;
}

static void commandTerminationHandler(void *parameter, ControlObjectClient connection)
{
    LastApplError lastApplError = ControlObjectClient_getLastApplError(connection);

    /* if lastApplError.error != 0 this indicates a CommandTermination- */
    if (lastApplError.error != 0) {
        printf("Received CommandTermination-.\n");
        printf(" LastApplError: %i\n", lastApplError.error);
        printf("      addCause: %i\n", lastApplError.addCause);
    }
    else
        printf("Received CommandTermination+.\n");
}

bool startsWith(const std::string& str, const std::string prefix) {
    return (str.rfind(prefix, 0) == 0);
}

bool endsWith(const std::string& str, const std::string suffix) {
    if (suffix.length() > str.length()) { return false; }

    return (str.rfind(suffix) == (str.length() - suffix.length()));
}

//通过设备中的逻辑设备名和数据块中的逻辑设备名比较得到IEDNAME
std::string getIedNameFromServer(IedConnection connection,std::string &strLDName)
{
    string strIEDName;
    MmsConnection mmsConnection = IedConnection_getMmsConnection(connection);
    if (mmsConnection == NULL)
    {
        printf("CIEC61850:IedConnection_getMmsConnection 失败.\n");
        return strIEDName;
    }

    //获取设备服务中的逻辑设备列表
    MmsError mmsErr = MMS_ERROR_NONE;
    LinkedList listDevNames = MmsConnection_getDomainNames(mmsConnection, &mmsErr);
    if (mmsErr != MMS_ERROR_NONE || listDevNames == NULL)
    {
        printf("CIEC61850:MmsConnection_getDomainNames 失败\n");
        return strIEDName;
    }

    vector<string> vecDevName;
    for (int i = 0; i < LinkedList_size(listDevNames); i++)
    {
        LinkedList curList = LinkedList_get(listDevNames, i);
        if (curList == NULL)
        {
            continue;
        }
        string strCurLDName = (char*)curList->data;
        vecDevName.push_back(strCurLDName);
        printf("CIEC61850:当前服务器中包含逻辑设备:%s\n", strCurLDName.c_str());
    }

    LinkedList_destroy(listDevNames);
    listDevNames = NULL;

    //找到数据块中配置的其中一个逻辑设备名,1.最开始已经判断是否为空，2.加载数据块时已经判断了逻辑设备名是否为空
    string strSvrLDName;
    for (size_t i = 0; i < vecDevName.size(); i++)
    {
        if (endsWith(vecDevName[i], strLDName))
        {
            strSvrLDName = vecDevName[i];
            break;
        }
    }

    if (strSvrLDName.empty())
    {
        printf("CIEC61850:当前服务器中不包含逻辑设备:%s\n", strLDName.c_str());
        return strIEDName;
    }

    strIEDName = strSvrLDName.substr(0,strSvrLDName.rfind(strLDName));
    printf("CIEC61850:获取IedName成功.IedName=%s\n", strIEDName.c_str());
    return strIEDName;
}

int uninstallOneReportCallBack(IedConnection con, S61850ReportParam &stReportParam)
{
    if (NULL == stReportParam.rcb)
    {
        return 0;
    }

    IedClientError error = IED_ERROR_OK;

    /* disable reporting */
    ClientReportControlBlock_setRptEna(stReportParam.rcb, false);
    IedConnection_setRCBValues(con, &error, stReportParam.rcb, RCB_ELEMENT_RPT_ENA, true);
    if (error != IED_ERROR_OK)
    {
        printf("CIEC61850:禁用report失败.report=%s  real_report=%s\n",stReportParam.strRCB.c_str(), stReportParam.strRealRCB.c_str());
    }

    destoryReportResource(con,stReportParam);

    return 0;
}


int main(int argc, char** argv)
{
    char* hostname;
    int tcpPort = 102;

    if (argc > 1)
        hostname = argv[1];
    else
        hostname = "localhost";

    if (argc > 2)
        tcpPort = atoi(argv[2]);

    running = 1;

    signal(SIGINT, sigint_handler);

    IedClientError error;
    ClientReportControlBlock rcb = NULL;
    ClientDataSet clientDataSet = NULL;
    LinkedList dataSetDirectory = NULL;

    IedConnection con = IedConnection_create();
    /* To change MMS parameters you need to get access to the underlying MmsConnection */
    MmsConnection mmsConnection = IedConnection_getMmsConnection(con);

    /* Get the container for the parameters */
    IsoConnectionParameters parameters = MmsConnection_getIsoConnectionParameters(mmsConnection);
    IedConnection_setConnectTimeout(con, 10 * 1000);
    IedConnection_setRequestTimeout(con, 15 * 1000);
    IedConnection_connect(con, &error, hostname, tcpPort);

    if (error == IED_ERROR_OK) {
        
        string strLDName = "MEAS";
        string strIEDName = getIedNameFromServer(con, strLDName);

        S61850ReportParam stReportParam;
        stReportParam.dwRCBMask = RCB_ELEMENT_TRG_OPS | RCB_ELEMENT_OPT_FLDS | RCB_ELEMENT_RPT_ENA;
        stReportParam.strRealRCB = strIEDName + "MEAS/LLN0.RP.urcbMeasMX01";
        stReportParam.strRealRcbRef = strIEDName + "MEAS/LLN0.RP.urcbMeasMX";
        stReportParam.strRealDataSetDir = strIEDName + "MEAS/LLN0.dsmeasMX";
        stReportParam.strRealDataSetRef = strIEDName + "MEAS/LLN0$dsmeasMX";

        installOneReportCallBack(con, stReportParam);

        int nCount = 0;
        while (running) {
            Thread_sleep(1000);
            if (nCount++ % 10 == 0)
            {
                enableOneReportGI(con, stReportParam);
            }

            IedConnectionState conState = IedConnection_getState(con);

            if (conState != IED_STATE_CONNECTED) {
                printf("Connection closed by server!\n");
                running = 0;
            }
        }

        /* disable reporting */
        if (con != NULL)
        {
            uninstallOneReportCallBack(con, stReportParam);
        }
        
        IedConnection_close(con);
    }
    else {
        printf("Failed to connect to %s:%i\n", hostname, tcpPort);
    }

    IedConnection_destroy(con);
    return 0;
}


