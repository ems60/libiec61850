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
    bool bUsed;     //�Ƿ����ã�Ĭ�����ã�������ݼ�û���κβ�㱻ʹ�þͽ��õ�
    std::string strIedName;
    std::string strLDName; //�߼��豸����������IEDNAME����
    std::string strDataSetDir;  //����iedName MEAS/LLN0.dsAin
    std::string strDataSetRef;  //����iedname MEAS/LLN0$dsAin
    std::string strRCB;         //����iedname MEAS/LLN0.RP.brcbAin01
    std::string strRcbRef;      //����iedname MEAS/LLN0.RP.brcbAin
    std::string strRealDataSetDir;  //��iedName EMUMEAS/LLN0.dsAin
    std::string strRealDataSetRef;  //��iedname EMUMEAS/LLN0$dsAin
    std::string strRealRCB;         //��iedname EMUMEAS/LLN0.RP.brcbAin01
    std::string strRealRcbRef;      //��iedname EMUMEAS/LLN0.RP.brcbAin
    uint32_t dwRCBMask;               //�������屨���а�����Щ��Ϣ

    ClientReportControlBlock rcb;

    _S61850ReportParam() {
        bUsed = true;
        dwRCBMask = 0;
        rcb = NULL;
    }
}S61850ReportParam, *PS61850ReportParam;

//�ṹ������mms���ݹ鵽��һ��ֵ����
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
        if (nIdx >= static_cast<int>(MmsValue_getArraySize(tempValue)))  //������Χ
        {
            return false;
        }
        tempValue = MmsValue_getElement(tempValue, nIdx);
        return getValueFromMmsValue(tempValue, nIdx, retValue);
    case MMS_BOOLEAN:
        retValue = static_cast<T>(MmsValue_getBoolean(curValue));
        return true;
    case MMS_INTEGER: //ͬʱ����32λ��64λ
        retValue = static_cast<T>(MmsValue_toInt64(curValue));
        return true;
    case MMS_UNSIGNED:
        retValue = static_cast<T>(MmsValue_toUint32(curValue));
        return true;
    case MMS_FLOAT://����ֻ������32λfloat��û�н���64λfloat
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

    //�ϴ�ԭ��:���ݱ仯(1)|Ʒ�ʱ仯(2)|���ݸ���(3)|��ʱ�ϱ�(4)|����(5)
    if (MmsValue_getBitStringBit(value, 1))
    {
        return true;
    }

    return false;
}

void reportCallbackFunction(void* parameter, ClientReport clientReport)
{
    if (!running)//���յ�ϵͳ�˳�������������ݡ�
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
        printf("��ǰ����������ݼ�����Ϊ��.\n");
        return;
    }

    string strDataSetName = ClientReport_getDataSetName(clientReport); //���ݼ�����
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

    int nValueIdx = ClientReport_getValueIndex(clientReport);  //ֵ����ʼ��ַ,��1���ǲ���ʶ����2����ֵ����3����ԭ��
    int nElementNum = MmsValue_getNumberOfSetBits(inclusion);

    //У��һ���ܳ��ȣ���ֹԽ��
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

        printf("���յ�����: ref=%s", strDataRef.c_str());
        float fValue = 0.0;
        if (getValueFromMmsValue(curValue, 0, fValue))
        {
            printf(" ֵ=%f", fValue);
        }

        Quality nQulity = 0;
        if (getValueFromMmsValue(curValue, 1, nQulity))
        {
            printf(" Ʒ��=%d", nQulity);
        }

        uint64_t lTime = 0L;
        if (getValueFromMmsValue(curValue, 2, lTime))
        {
            printf(" ʱ���=%lld", lTime);
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
        printf("CIEC61850: IedConnection_getRCBValuesʧ��,error=%d.\n", error);
        destoryReportResource(con,stReportParam);
        return -1;
    }

    /* prepare the parameters of the RCP */
    ClientReportControlBlock_setResv(rcb, true);
    ClientReportControlBlock_setTrgOps(rcb, TRG_OPT_DATA_CHANGED | TRG_OPT_QUALITY_CHANGED | TRG_OPT_GI);
    ClientReportControlBlock_setOptFlds(rcb, RPT_OPT_TIME_STAMP | RPT_OPT_REASON_FOR_INCLUSION | RPT_OPT_DATA_SET | RPT_OPT_DATA_REFERENCE | RPT_OPT_ENTRY_ID);
    ClientReportControlBlock_setDataSetReference(rcb, stReportParam.strRealDataSetRef.c_str()); /* NOTE the "$" instead of "." ! */
    //ClientReportControlBlock_setRptId(rcb, stReportParam.strRealRCB.c_str()); //��������Ҫ���ã������еĳ����ڱ���鱨����ʹ�õ��Ǳ�����ʵ����

    ClientReportControlBlock_setRptEna(rcb, true);
    //����ָ������ͣ����Դ˴��Ͳ���ֵ��
    //ClientReportControlBlock_setGI(rcb, true);

    /* Configure the report receiver */
    IedConnection_installReportHandler(con, stReportParam.strRealRcbRef.c_str(),
        ClientReportControlBlock_getRptId(rcb),reportCallbackFunction, NULL);


    /* Write RCB parameters and enable report */
    IedConnection_setRCBValues(con, &error, rcb, stReportParam.dwRCBMask, true);
    if (error != IED_ERROR_OK)
    {
        printf("CIEC61850: IedConnection_setRCBValuesʧ��,rcb=%s error=%d\n", stReportParam.strRealRcbRef.c_str(), error);
        IedConnection_uninstallReportHandler(con, stReportParam.strRealRcbRef.c_str());
        destoryReportResource(con,stReportParam);
        return -1;
    }

    printf("CIEC61850:����report�ɹ�.report=%s  real_report=%s\n",stReportParam.strRCB.c_str(), stReportParam.strRealRCB.c_str());

    return 0;
}

int enableOneReportGI(IedConnection con, S61850ReportParam &stReportParam)
{
    if (NULL == stReportParam.rcb)
    {
        return -1;
    }

    IedClientError error = IED_ERROR_OK;
    /* ���� */
    ClientReportControlBlock_setGI(stReportParam.rcb, true);
    IedConnection_setRCBValues(con, &error, stReportParam.rcb, RCB_ELEMENT_GI, true);
    if (error != IED_ERROR_OK)
    {
        printf("CIEC61850:��������ʧ��.dataset=%s  real_dataset=%s\n",
            stReportParam.strDataSetRef.c_str(), stReportParam.strRealDataSetRef.c_str());
    }
    else
    {
        printf("CIEC61850:�������ٳɹ�.dataset=%s  real_dataset=%s\n",
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

//ͨ���豸�е��߼��豸�������ݿ��е��߼��豸���Ƚϵõ�IEDNAME
std::string getIedNameFromServer(IedConnection connection,std::string &strLDName)
{
    string strIEDName;
    MmsConnection mmsConnection = IedConnection_getMmsConnection(connection);
    if (mmsConnection == NULL)
    {
        printf("CIEC61850:IedConnection_getMmsConnection ʧ��.\n");
        return strIEDName;
    }

    //��ȡ�豸�����е��߼��豸�б�
    MmsError mmsErr = MMS_ERROR_NONE;
    LinkedList listDevNames = MmsConnection_getDomainNames(mmsConnection, &mmsErr);
    if (mmsErr != MMS_ERROR_NONE || listDevNames == NULL)
    {
        printf("CIEC61850:MmsConnection_getDomainNames ʧ��\n");
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
        printf("CIEC61850:��ǰ�������а����߼��豸:%s\n", strCurLDName.c_str());
    }

    LinkedList_destroy(listDevNames);
    listDevNames = NULL;

    //�ҵ����ݿ������õ�����һ���߼��豸��,1.�ʼ�Ѿ��ж��Ƿ�Ϊ�գ�2.�������ݿ�ʱ�Ѿ��ж����߼��豸���Ƿ�Ϊ��
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
        printf("CIEC61850:��ǰ�������в������߼��豸:%s\n", strLDName.c_str());
        return strIEDName;
    }

    strIEDName = strSvrLDName.substr(0,strSvrLDName.rfind(strLDName));
    printf("CIEC61850:��ȡIedName�ɹ�.IedName=%s\n", strIEDName.c_str());
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
        printf("CIEC61850:����reportʧ��.report=%s  real_report=%s\n",stReportParam.strRCB.c_str(), stReportParam.strRealRCB.c_str());
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


