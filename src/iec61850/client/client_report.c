/*
 *  client_report.c
 *
 *  Client implementation for IEC 61850 reporting.
 *
 *  Copyright 2013-2022 Michael Zillgith
 *
 *  This file is part of libIEC61850.
 *
 *  libIEC61850 is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  libIEC61850 is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with libIEC61850.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  See COPYING file for the complete license text.
 */

#include "iec61850_client.h"

#include "stack_config.h"

#include "ied_connection_private.h"

#include "libiec61850_platform_includes.h"

struct sClientReport
{
    ReportCallbackFunction callback;
    void* callbackParameter;
    char* rcbReference;
    char* rptId;

    char* dataSetName;
    int dataSetNameSize; /* size of the dataSetName buffer */

    int dataSetSize;
    int valueIndex; /* 本报告中值索引 */

    MmsValue* entryId;
    /*
     * 为了提高性能，dataReferences和dataSetValues没有赋值，改为使用dataValues和valueIndex配合
     * 相当于直接将报告中的变量地址暴露到业务从注册的回调函数处，这样就不用经过大量遍历了运算了
     */
    MmsValue* dataReferences;
    MmsValue* dataSetValues;
    MmsValue* inclusion; /* 标记本数据集包含哪些测点 */
    MmsValue* dataValues;    /* 本次报告中的报文缓存 */
    ReasonForInclusion* reasonForInclusion;

    /* Presence flags for optional elements */
    bool hasDataSetName;
    bool hasReasonForInclusion;
    bool hasSequenceNumber;
    bool hasDataReference;
    bool hasConfRev;
    bool hasTimestamp;
    bool hasBufOverflow;
    bool hasSubSequenceNumber;

    uint64_t timestamp;
    uint16_t seqNum;
    uint32_t confRev;
    bool bufOverflow;

    uint16_t subSeqNum;
    bool moreSegementsFollow;
};

char*
ReasonForInclusion_getValueAsString(ReasonForInclusion reasonCode)
{
    switch (reasonCode) {
    case IEC61850_REASON_NOT_INCLUDED:
        return "not-included";
    case IEC61850_REASON_DATA_CHANGE:
        return "data-change";
    case IEC61850_REASON_DATA_UPDATE:
        return "data-update";
    case IEC61850_REASON_QUALITY_CHANGE:
        return "quality-change";
    case IEC61850_REASON_GI:
        return "GI";
    case IEC61850_REASON_INTEGRITY:
        return "integrity";
    default:
        return "unknown";
    }
}

ClientReport
ClientReport_create()
{
    ClientReport self = (ClientReport) GLOBAL_CALLOC(1, sizeof(struct sClientReport));

    self->dataSetSize = -1;

    return self;
}

void
ClientReport_destroy(ClientReport self)
{
    if (self->entryId)
        MmsValue_delete(self->entryId);

    GLOBAL_FREEMEM(self->rcbReference);

    if (self->rptId != NULL)
        GLOBAL_FREEMEM(self->rptId);

    if (self->dataSetValues != NULL)
        MmsValue_delete(self->dataSetValues);

    if (self->dataReferences != NULL)
        MmsValue_delete(self->dataReferences);

    if (self->reasonForInclusion != NULL)
        GLOBAL_FREEMEM(self->reasonForInclusion);

    if (self->dataSetName != NULL)
    	GLOBAL_FREEMEM((void*) self->dataSetName);

    GLOBAL_FREEMEM(self);
}

char*
ClientReport_getRcbReference(ClientReport self)
{
    return self->rcbReference;
}

char*
ClientReport_getRptId(ClientReport self)
{
    return self->rptId;
}

ReasonForInclusion
ClientReport_getReasonForInclusion(ClientReport self, int elementIndex)
{
    if (self->reasonForInclusion != NULL)
        return self->reasonForInclusion[elementIndex];
    else
        return IEC61850_REASON_NOT_INCLUDED;
}

MmsValue*
ClientReport_getEntryId(ClientReport self)
{
    return self->entryId;
}

bool
ClientReport_hasTimestamp(ClientReport self)
{
    return self->hasTimestamp;
}

uint64_t
ClientReport_getTimestamp(ClientReport self)
{
    return self->timestamp;
}


bool
ClientReport_hasSeqNum(ClientReport self)
{
    return self->hasSequenceNumber;
}

uint16_t
ClientReport_getSeqNum(ClientReport self)
{
    return self->seqNum;
}

bool
ClientReport_hasDataSetName(ClientReport self)
{
    return self->hasDataSetName;
}

bool
ClientReport_hasReasonForInclusion(ClientReport self)
{
    return self->hasReasonForInclusion;
}

bool
ClientReport_hasConfRev(ClientReport self)
{
    return self->hasConfRev;
}

uint32_t
ClientReport_getConfRev(ClientReport self)
{
    return self->confRev;
}

bool
ClientReport_hasBufOvfl(ClientReport self)
{
    return self->hasBufOverflow;
}

bool
ClientReport_getBufOvfl(ClientReport self)
{
    return self->bufOverflow;
}

bool
ClientReport_hasDataReference(ClientReport self)
{
    return self->hasDataReference;
}

const char*
ClientReport_getDataReference(ClientReport self, int elementIndex)
{
    char* dataReference = NULL;

    if (self->dataReferences != NULL) {
        MmsValue* dataRefValue = MmsValue_getElement(self->dataReferences, elementIndex);

        if (dataRefValue != NULL) {
            if (MmsValue_getType(dataRefValue) == MMS_VISIBLE_STRING) {
                return MmsValue_toString(dataRefValue);
            }
        }
    }

    return dataReference;
}

const char*
ClientReport_getDataSetName(ClientReport self)
{
	return self->dataSetName;
}

MmsValue*
ClientReport_getDataSetValues(ClientReport self)
{
    return self->dataSetValues;
}

MmsValue* 
ClientReport_getDataValues(ClientReport self)
{
    return self->dataValues;
}

MmsValue*
ClientReport_getInclusion(ClientReport self)
{
    return self->inclusion;
}

int
ClientReport_getValueIndex(ClientReport self)
{
    return self->valueIndex;
}

bool
ClientReport_hasSubSeqNum(ClientReport self)
{
    return self->hasSubSequenceNumber;
}

uint16_t
ClientReport_getSubSeqNum(ClientReport self)
{
    return self->subSeqNum;
}

bool
ClientReport_getMoreSeqmentsFollow(ClientReport self)
{
    return self->moreSegementsFollow;
}

static ClientReport
lookupReportHandler(IedConnection self, const char* rcbReference)
{
    LinkedList element = LinkedList_getNext(self->enabledReports);

    while (element != NULL) {
        ClientReport report = (ClientReport) element->data;

        if (strcmp(report->rcbReference, rcbReference) == 0)
            return report;

        element = LinkedList_getNext(element);
    }

    return NULL;
}

void
IedConnection_installReportHandler(IedConnection self, const char* rcbReference, const char* rptId, ReportCallbackFunction handler,
        void* handlerParameter)
{
    ClientReport report = lookupReportHandler(self, rcbReference);

    if (report != NULL) {
        IedConnection_uninstallReportHandler(self, rcbReference);

        if (DEBUG_IED_CLIENT)
            printf("DEBUG_IED_CLIENT: Removed existing report callback handler for %s\n", rcbReference);
    }

    report = ClientReport_create();
    report->callback = handler;
    report->callbackParameter = handlerParameter;
    report->rcbReference = StringUtils_copyString(rcbReference);

    if (rptId != NULL)
        report->rptId = StringUtils_copyString(rptId);
    else
        report->rptId = NULL;

    Semaphore_wait(self->reportHandlerMutex);
    LinkedList_add(self->enabledReports, report);
    Semaphore_post(self->reportHandlerMutex);

    if (DEBUG_IED_CLIENT)
        printf("DEBUG_IED_CLIENT: Installed new report callback handler for %s\n", rcbReference);
}

void
IedConnection_uninstallReportHandler(IedConnection self, const char* rcbReference)
{
	Semaphore_wait(self->reportHandlerMutex);

    ClientReport report = lookupReportHandler(self, rcbReference);

    if (report != NULL) {
        LinkedList_remove(self->enabledReports, report);
        ClientReport_destroy(report);
    }

    Semaphore_post(self->reportHandlerMutex);
}

void
IedConnection_triggerGIReport(IedConnection self, IedClientError* error, const char* rcbReference)
{
    char domainId[65];
    char itemId[65];

    MmsMapping_getMmsDomainFromObjectReference(rcbReference, domainId);

    StringUtils_concatString(itemId, 65, rcbReference + strlen(domainId) + 1, "");

    StringUtils_replace(itemId, '.', '$');

    StringUtils_appendString(itemId, 65, "$GI");

    MmsConnection mmsCon = IedConnection_getMmsConnection(self);

    MmsError mmsError;

    MmsValue* gi = MmsValue_newBoolean(true);

    MmsConnection_writeVariable(mmsCon, &mmsError, domainId, itemId, gi);

    MmsValue_delete(gi);

    if (mmsError != MMS_ERROR_NONE) {
        if (DEBUG_IED_CLIENT)
            printf("DEBUG_IED_CLIENT: failed to trigger GI for %s!\n", rcbReference);

        *error = iedConnection_mapMmsErrorToIedError(mmsError);
    }
    else {
        *error = IED_ERROR_OK;
    }
}

#if 0
void
iedConnection_handleReport(IedConnection self, MmsValue* value)
{
    MmsValue* rptIdValue = MmsValue_getElement(value, 0);

    if ((rptIdValue == NULL) || (MmsValue_getType(rptIdValue) != MMS_VISIBLE_STRING)) {
        if (DEBUG_IED_CLIENT)
            printf("IED_CLIENT: received malformed report (RptId)\n");

        goto exit_function;
    }

    LinkedList element = LinkedList_getNext(self->enabledReports);
    ClientReport matchingReport = NULL;

    while (element != NULL) {
        ClientReport report = (ClientReport) element->data;
        char defaultRptId[130];
        char* rptId = report->rptId;

        if ((rptId == NULL) || (strlen(rptId) == 0)) {
            StringUtils_concatString(defaultRptId, 130, report->rcbReference, "");
            StringUtils_replace(defaultRptId, '.', '$');

            rptId = defaultRptId;
        }

        if (strcmp(MmsValue_toString(rptIdValue), rptId) == 0) {
            matchingReport = report;
            break;
        }

        element = LinkedList_getNext(element);
    }

    if (matchingReport == NULL)
        goto exit_function;

    matchingReport->hasSequenceNumber = false;
    matchingReport->hasTimestamp = false;
    matchingReport->hasReasonForInclusion = false;
    matchingReport->hasDataReference = false;
    matchingReport->hasConfRev = false;
    matchingReport->hasDataSetName = false;
    matchingReport->hasBufOverflow = false;
    matchingReport->hasSubSequenceNumber = false;

   if (DEBUG_IED_CLIENT)
        printf("IED_CLIENT: received report with ID %s\n", MmsValue_toString(rptIdValue));

    MmsValue* optFlds = MmsValue_getElement(value, 1);

    if ((optFlds == NULL) || (MmsValue_getType(optFlds) != MMS_BIT_STRING)) {
        if (DEBUG_IED_CLIENT)
            printf("IED_CLIENT: received malformed report (OptFlds)\n");

        goto exit_function;
    }

    int inclusionIndex = 2;

    /* has sequence-number */
    if (MmsValue_getBitStringBit(optFlds, 1) == true) {

        MmsValue* seqNum = MmsValue_getElement(value, inclusionIndex);

        if ((seqNum == NULL) || (MmsValue_getType(seqNum) != MMS_UNSIGNED)) {
            if (DEBUG_IED_CLIENT)
                printf("IED_CLIENT: received malformed report (seqNum)\n");

            goto exit_function;
        }

        matchingReport->seqNum = (uint16_t) MmsValue_toUint32(seqNum);
        matchingReport->hasSequenceNumber = true;

        inclusionIndex++;
    }

    /* has report-timestamp */
    if (MmsValue_getBitStringBit(optFlds, 2) == true) {

        MmsValue* timeStampValue = MmsValue_getElement(value, inclusionIndex);

        if ((timeStampValue == NULL) || (MmsValue_getType(timeStampValue) != MMS_BINARY_TIME)) {
            if (DEBUG_IED_CLIENT)
                printf("IED_CLIENT: received malformed report (timeStamp)\n");

            goto exit_function;
        }

        matchingReport->hasTimestamp = true;
        matchingReport->timestamp = MmsValue_getBinaryTimeAsUtcMs(timeStampValue);

        if (DEBUG_IED_CLIENT)
            printf("IED_CLIENT: report has timestamp %llu\n", (unsigned long long) matchingReport->timestamp);

        inclusionIndex++;
    }

    /* check if data set name is present */
    if (MmsValue_getBitStringBit(optFlds, 4) == true) {
        matchingReport->hasDataSetName = true;

        MmsValue* dataSetName = MmsValue_getElement(value, inclusionIndex);

        if ((dataSetName == NULL) || (MmsValue_getType(dataSetName) != MMS_VISIBLE_STRING)) {
            if (DEBUG_IED_CLIENT)
                printf("IED_CLIENT: received malformed report (DatSet)\n");

            goto exit_function;
        }

        int dataSetNameSize = MmsValue_getStringSize(dataSetName);

        /* limit to prevent large memory allocation */
        if (dataSetNameSize < 130) {
            const char* dataSetNameStr = MmsValue_toString(dataSetName);

            if (matchingReport->dataSetName == NULL) {
                matchingReport->dataSetName = (char*) GLOBAL_MALLOC(dataSetNameSize + 1);

                if (matchingReport->dataSetName == NULL) {
                    matchingReport->dataSetNameSize =  0;

                    if (DEBUG_IED_CLIENT)
                        printf("IED_CLIENT: failed to allocate memory\n");

                    goto exit_function;
                }

                matchingReport->dataSetNameSize = dataSetNameSize + 1;
            }
            else {
                if (matchingReport->dataSetNameSize < MmsValue_getStringSize(dataSetName) + 1) {
                    GLOBAL_FREEMEM((void*) matchingReport->dataSetName);

                    matchingReport->dataSetName = (char*) GLOBAL_MALLOC(dataSetNameSize + 1);

                    if (matchingReport->dataSetName == NULL) {
                        matchingReport->dataSetNameSize =  0;

                        if (DEBUG_IED_CLIENT)
                            printf("IED_CLIENT: failed to allocate memory\n");

                        goto exit_function;
                    }

                    matchingReport->dataSetNameSize = dataSetNameSize + 1;
                }
            }

            StringUtils_copyStringMax(matchingReport->dataSetName, dataSetNameSize + 1, dataSetNameStr);
        }
        else {
            if (DEBUG_IED_CLIENT)
                printf("IED_CLIENT: report DatSet name too large (%i)\n", dataSetNameSize);

            goto exit_function;
        }

        inclusionIndex++;
    }

    if (DEBUG_IED_CLIENT)
        printf("IED_CLIENT: Found enabled report!\n");

    /* check bufOvfl */
    if (MmsValue_getBitStringBit(optFlds, 6) == true) {
        MmsValue* bufOverflow = MmsValue_getElement(value, inclusionIndex);

        if ((bufOverflow == NULL) || (MmsValue_getType(bufOverflow) != MMS_BOOLEAN)) {
            if (DEBUG_IED_CLIENT)
                printf("IED_CLIENT: received malformed report (BufOvfl)\n");

            goto exit_function;
        }

        matchingReport->hasBufOverflow = true;
        matchingReport->bufOverflow = MmsValue_getBoolean(bufOverflow);

        inclusionIndex++;
    }

    /* check for entryId */
    if (MmsValue_getBitStringBit(optFlds, 7) == true) {
        MmsValue* entryId = MmsValue_getElement(value, inclusionIndex);

        if ((entryId == NULL) || (MmsValue_getType(entryId) != MMS_OCTET_STRING)) {
            if (DEBUG_IED_CLIENT)
                printf("IED_CLIENT: received malformed report (entryID)\n");

            goto exit_function;
        }

        if (matchingReport->entryId != NULL) {

            if (!MmsValue_update(matchingReport->entryId, entryId)) {
                MmsValue_delete(matchingReport->entryId);
                matchingReport->entryId = MmsValue_clone(entryId);
            }
        }
        else {
            matchingReport->entryId = MmsValue_clone(entryId);
        }

        inclusionIndex++;
    }

    /* check for confRev */
    if (MmsValue_getBitStringBit(optFlds, 8) == true) {
        MmsValue* confRev = MmsValue_getElement(value, inclusionIndex);

        if ((confRev == NULL) || (MmsValue_getType(confRev) != MMS_UNSIGNED)) {
            if (DEBUG_IED_CLIENT)
                printf("IED_CLIENT: received malformed report (confRev)\n");

            goto exit_function;
        }

        matchingReport->confRev = MmsValue_toUint32(confRev);
        matchingReport->hasConfRev = true;

        inclusionIndex++;
    }

    /* handle segmentation fields (check ReportedOptFlds.segmentation) */
    if (MmsValue_getBitStringBit(optFlds, 9) == true) {

        MmsValue* subSeqNum = MmsValue_getElement(value, inclusionIndex);
        inclusionIndex++;

        if ((subSeqNum == NULL) || (MmsValue_getType(subSeqNum) != MMS_UNSIGNED)) {
            if (DEBUG_IED_CLIENT)
                printf("IED_CLIENT: received malformed report (SubSeqNum)\n");

            goto exit_function;
        }
        else {
            matchingReport->subSeqNum = (uint16_t) MmsValue_toUint32(subSeqNum);
        }

        MmsValue* moreSegmentsFollow = MmsValue_getElement(value, inclusionIndex);
        inclusionIndex++;

        if ((moreSegmentsFollow == NULL) || (MmsValue_getType(moreSegmentsFollow) != MMS_BOOLEAN)) {
            if ((subSeqNum == NULL) || (MmsValue_getType(subSeqNum) != MMS_UNSIGNED)) {
                if (DEBUG_IED_CLIENT)
                    printf("IED_CLIENT: received malformed report (MoreSegmentsFollow)\n");

                goto exit_function;
            }
        }
        else {
            matchingReport->moreSegementsFollow = MmsValue_getBoolean(moreSegmentsFollow);
        }

        matchingReport->hasSequenceNumber = true;
    }
    else {
        matchingReport->subSeqNum = 0;
        matchingReport->moreSegementsFollow = false;
    }

    MmsValue* inclusion = MmsValue_getElement(value, inclusionIndex);

    if ((inclusion == NULL) || (MmsValue_getType(inclusion) != MMS_BIT_STRING)) {
        if (DEBUG_IED_CLIENT)
            printf("IED_CLIENT: received malformed report (inclusion)\n");

        goto exit_function;
    }

    int dataSetSize = MmsValue_getBitStringSize(inclusion);

    if (matchingReport->dataSetSize == -1) {
        matchingReport->dataSetSize = dataSetSize;
    }
    else {
        if (dataSetSize != matchingReport->dataSetSize) {
            if (DEBUG_IED_CLIENT)
                printf("IED_CLIENT: received malformed report (inclusion has no plausible size)\n");

            goto exit_function;
        }
    }

    int includedElements = MmsValue_getNumberOfSetBits(inclusion);

    if (DEBUG_IED_CLIENT)
        printf("IED_CLIENT: Report includes %i data set elements of %i\n", includedElements,
                dataSetSize);

    int valueIndex = inclusionIndex + 1;

    /* parse data-references if required */
    if (MmsValue_getBitStringBit(optFlds, 5) == true) {

        if (matchingReport->dataReferences == NULL)
            matchingReport->dataReferences = MmsValue_createEmptyArray(dataSetSize);

        matchingReport->hasDataReference = true;

        int elementIndex;

        for (elementIndex = 0; elementIndex < dataSetSize; elementIndex++) {
            if (MmsValue_getBitStringBit(inclusion, elementIndex) == true) {
                MmsValue* dataSetElement = MmsValue_getElement(matchingReport->dataReferences, elementIndex);

                if (dataSetElement == NULL) {

                    MmsValue* dataRefValue = MmsValue_getElement(value, valueIndex);

                    if ((dataRefValue == NULL) || (MmsValue_getType(dataRefValue) != MMS_VISIBLE_STRING)) {
                        if (DEBUG_IED_CLIENT)
                            printf("IED_CLIENT: report contains invalid data reference\n");
                    }
                    else {
                        dataSetElement = MmsValue_clone(dataRefValue);

                        MmsValue_setElement(matchingReport->dataReferences, elementIndex, dataSetElement);
                    }
                }

                valueIndex += 1;
            }
        }
    }

    int i;

    if (matchingReport->dataSetValues == NULL) {
        matchingReport->dataSetValues = MmsValue_createEmptyArray(dataSetSize);
        matchingReport->reasonForInclusion = (ReasonForInclusion*)
                GLOBAL_MALLOC(sizeof(ReasonForInclusion) * dataSetSize);

        int elementIndex;

        for (elementIndex = 0; elementIndex < dataSetSize; elementIndex++)
            matchingReport->reasonForInclusion[elementIndex] = IEC61850_REASON_NOT_INCLUDED;

    }

    MmsValue* dataSetValues = matchingReport->dataSetValues;

    bool hasReasonForInclusion = MmsValue_getBitStringBit(optFlds, 3);

    if (hasReasonForInclusion)
        matchingReport->hasReasonForInclusion = true;

    for (i = 0; i < dataSetSize; i++) {
        if (MmsValue_getBitStringBit(inclusion, i) == true) {

            MmsValue* dataSetElement = MmsValue_getElement(dataSetValues, i);

            MmsValue* newElementValue = MmsValue_getElement(value, valueIndex);

            if (newElementValue == NULL) {
                if (DEBUG_IED_CLIENT)
                    printf("IED_CLIENT: report is missing expected element value\n");

                goto exit_function;
            }

            if (dataSetElement == NULL)
                MmsValue_setElement(dataSetValues, i, MmsValue_clone(newElementValue));
            else
                MmsValue_update(dataSetElement, newElementValue);

            if (DEBUG_IED_CLIENT)
                printf("IED_CLIENT: update element value type: %i\n", MmsValue_getType(newElementValue));

            if (hasReasonForInclusion) {
                MmsValue* reasonForInclusion = MmsValue_getElement(value, includedElements + valueIndex);

                if ((reasonForInclusion == NULL) || (MmsValue_getType(reasonForInclusion) != MMS_BIT_STRING)) {
                    if (DEBUG_IED_CLIENT)
                        printf("IED_CLIENT: report contains invalid reason-for-inclusion\n");

                    goto exit_function;
                }

                matchingReport->reasonForInclusion[i] = IEC61850_REASON_NOT_INCLUDED;

                if (MmsValue_getBitStringBit(reasonForInclusion, 1) == true)
                    matchingReport->reasonForInclusion[i] |= (ReasonForInclusion) IEC61850_REASON_DATA_CHANGE;
                if (MmsValue_getBitStringBit(reasonForInclusion, 2) == true)
                    matchingReport->reasonForInclusion[i] |= IEC61850_REASON_QUALITY_CHANGE;
                if (MmsValue_getBitStringBit(reasonForInclusion, 3) == true)
                    matchingReport->reasonForInclusion[i] |= IEC61850_REASON_DATA_UPDATE;
                if (MmsValue_getBitStringBit(reasonForInclusion, 4) == true)
                    matchingReport->reasonForInclusion[i] |= IEC61850_REASON_INTEGRITY;
                if (MmsValue_getBitStringBit(reasonForInclusion, 5) == true)
                    matchingReport->reasonForInclusion[i] |= IEC61850_REASON_GI;
            }
            else {
                matchingReport->reasonForInclusion[i] = IEC61850_REASON_UNKNOWN;
            }

            valueIndex++;
        }
        else {
            matchingReport->reasonForInclusion[i] = IEC61850_REASON_NOT_INCLUDED;
        }
    }

    Semaphore_wait(self->reportHandlerMutex);
    
    if (matchingReport->callback != NULL)
        matchingReport->callback(matchingReport->callbackParameter, matchingReport);

    Semaphore_post(self->reportHandlerMutex);

exit_function:
    return;
}
#endif

void
iedConnection_handleReport(IedConnection self, MmsValue* value)
{
    MmsValue* rptIdValue = MmsValue_getElement(value, 0);

    if ((rptIdValue == NULL) || (MmsValue_getType(rptIdValue) != MMS_VISIBLE_STRING)) {
        if (DEBUG_IED_CLIENT)
            printf("IED_CLIENT: received malformed report (RptId)\n");

        goto exit_function;
    }

    LinkedList element = LinkedList_getNext(self->enabledReports);
    ClientReport matchingReport = NULL;

    while (element != NULL) {
        ClientReport report = (ClientReport)element->data;
        char defaultRptId[130];
        char* rptId = report->rptId;

        if ((rptId == NULL) || (strlen(rptId) == 0)) {
            StringUtils_concatString(defaultRptId, 130, report->rcbReference, "");
            StringUtils_replace(defaultRptId, '.', '$');

            rptId = defaultRptId;
        }

        if (strcmp(MmsValue_toString(rptIdValue), rptId) == 0) {
            matchingReport = report;
            break;
        }

        element = LinkedList_getNext(element);
    }

    if (matchingReport == NULL)
        goto exit_function;

    matchingReport->hasSequenceNumber = false;
    matchingReport->hasTimestamp = false;
    matchingReport->hasReasonForInclusion = false;
    matchingReport->hasDataReference = false;
    matchingReport->hasConfRev = false;
    matchingReport->hasDataSetName = false;
    matchingReport->hasBufOverflow = false;
    matchingReport->hasSubSequenceNumber = false;

    if (DEBUG_IED_CLIENT)
        printf("IED_CLIENT: received report with ID %s\n", MmsValue_toString(rptIdValue));

    MmsValue* optFlds = MmsValue_getElement(value, 1);

    if ((optFlds == NULL) || (MmsValue_getType(optFlds) != MMS_BIT_STRING)) {
        if (DEBUG_IED_CLIENT)
            printf("IED_CLIENT: received malformed report (OptFlds)\n");

        goto exit_function;
    }

    int inclusionIndex = 2;

    /* has sequence-number */
    if (MmsValue_getBitStringBit(optFlds, 1) == true) {

        MmsValue* seqNum = MmsValue_getElement(value, inclusionIndex);

        if ((seqNum == NULL) || (MmsValue_getType(seqNum) != MMS_UNSIGNED)) {
            if (DEBUG_IED_CLIENT)
                printf("IED_CLIENT: received malformed report (seqNum)\n");

            goto exit_function;
        }

        matchingReport->seqNum = (uint16_t)MmsValue_toUint32(seqNum);
        matchingReport->hasSequenceNumber = true;

        inclusionIndex++;
    }

    /* has report-timestamp */
    if (MmsValue_getBitStringBit(optFlds, 2) == true) {

        MmsValue* timeStampValue = MmsValue_getElement(value, inclusionIndex);

        if ((timeStampValue == NULL) || (MmsValue_getType(timeStampValue) != MMS_BINARY_TIME)) {
            if (DEBUG_IED_CLIENT)
                printf("IED_CLIENT: received malformed report (timeStamp)\n");

            goto exit_function;
        }

        matchingReport->hasTimestamp = true;
        matchingReport->timestamp = MmsValue_getBinaryTimeAsUtcMs(timeStampValue);

        if (DEBUG_IED_CLIENT)
            printf("IED_CLIENT: report has timestamp %llu\n", (unsigned long long) matchingReport->timestamp);

        inclusionIndex++;
    }

    /* check if data set name is present */
    if (MmsValue_getBitStringBit(optFlds, 4) == true) {
        matchingReport->hasDataSetName = true;

        MmsValue* dataSetName = MmsValue_getElement(value, inclusionIndex);

        if ((dataSetName == NULL) || (MmsValue_getType(dataSetName) != MMS_VISIBLE_STRING)) {
            if (DEBUG_IED_CLIENT)
                printf("IED_CLIENT: received malformed report (DatSet)\n");

            goto exit_function;
        }

        int dataSetNameSize = MmsValue_getStringSize(dataSetName);

        /* limit to prevent large memory allocation */
        if (dataSetNameSize < 130) {
            const char* dataSetNameStr = MmsValue_toString(dataSetName);

            if (matchingReport->dataSetName == NULL) {
                matchingReport->dataSetName = (char*)GLOBAL_MALLOC(dataSetNameSize + 1);

                if (matchingReport->dataSetName == NULL) {
                    matchingReport->dataSetNameSize = 0;

                    if (DEBUG_IED_CLIENT)
                        printf("IED_CLIENT: failed to allocate memory\n");

                    goto exit_function;
                }

                matchingReport->dataSetNameSize = dataSetNameSize + 1;
            }
            else {
                if (matchingReport->dataSetNameSize < MmsValue_getStringSize(dataSetName) + 1) {
                    GLOBAL_FREEMEM((void*)matchingReport->dataSetName);

                    matchingReport->dataSetName = (char*)GLOBAL_MALLOC(dataSetNameSize + 1);

                    if (matchingReport->dataSetName == NULL) {
                        matchingReport->dataSetNameSize = 0;

                        if (DEBUG_IED_CLIENT)
                            printf("IED_CLIENT: failed to allocate memory\n");

                        goto exit_function;
                    }

                    matchingReport->dataSetNameSize = dataSetNameSize + 1;
                }
            }

            StringUtils_copyStringMax(matchingReport->dataSetName, dataSetNameSize + 1, dataSetNameStr);
        }
        else {
            if (DEBUG_IED_CLIENT)
                printf("IED_CLIENT: report DatSet name too large (%i)\n", dataSetNameSize);

            goto exit_function;
        }

        inclusionIndex++;
    }

    if (DEBUG_IED_CLIENT)
        printf("IED_CLIENT: Found enabled report!\n");

    /* check bufOvfl */
    if (MmsValue_getBitStringBit(optFlds, 6) == true) {
        MmsValue* bufOverflow = MmsValue_getElement(value, inclusionIndex);

        if ((bufOverflow == NULL) || (MmsValue_getType(bufOverflow) != MMS_BOOLEAN)) {
            if (DEBUG_IED_CLIENT)
                printf("IED_CLIENT: received malformed report (BufOvfl)\n");

            goto exit_function;
        }

        matchingReport->hasBufOverflow = true;
        matchingReport->bufOverflow = MmsValue_getBoolean(bufOverflow);

        inclusionIndex++;
    }

    /* check for entryId */
    if (MmsValue_getBitStringBit(optFlds, 7) == true) {
        MmsValue* entryId = MmsValue_getElement(value, inclusionIndex);

        if ((entryId == NULL) || (MmsValue_getType(entryId) != MMS_OCTET_STRING)) {
            if (DEBUG_IED_CLIENT)
                printf("IED_CLIENT: received malformed report (entryID)\n");

            goto exit_function;
        }

        if (matchingReport->entryId != NULL) {

            if (!MmsValue_update(matchingReport->entryId, entryId)) {
                MmsValue_delete(matchingReport->entryId);
                matchingReport->entryId = MmsValue_clone(entryId);
            }
        }
        else {
            matchingReport->entryId = MmsValue_clone(entryId);
        }

        inclusionIndex++;
    }

    /* check for confRev */
    if (MmsValue_getBitStringBit(optFlds, 8) == true) {
        MmsValue* confRev = MmsValue_getElement(value, inclusionIndex);

        if ((confRev == NULL) || (MmsValue_getType(confRev) != MMS_UNSIGNED)) {
            if (DEBUG_IED_CLIENT)
                printf("IED_CLIENT: received malformed report (confRev)\n");

            goto exit_function;
        }

        matchingReport->confRev = MmsValue_toUint32(confRev);
        matchingReport->hasConfRev = true;

        inclusionIndex++;
    }

    /* handle segmentation fields (check ReportedOptFlds.segmentation) */
    if (MmsValue_getBitStringBit(optFlds, 9) == true) {

        MmsValue* subSeqNum = MmsValue_getElement(value, inclusionIndex);
        inclusionIndex++;

        if ((subSeqNum == NULL) || (MmsValue_getType(subSeqNum) != MMS_UNSIGNED)) {
            if (DEBUG_IED_CLIENT)
                printf("IED_CLIENT: received malformed report (SubSeqNum)\n");

            goto exit_function;
        }
        else {
            matchingReport->subSeqNum = (uint16_t)MmsValue_toUint32(subSeqNum);
        }

        MmsValue* moreSegmentsFollow = MmsValue_getElement(value, inclusionIndex);
        inclusionIndex++;

        if ((moreSegmentsFollow == NULL) || (MmsValue_getType(moreSegmentsFollow) != MMS_BOOLEAN)) {
            if ((subSeqNum == NULL) || (MmsValue_getType(subSeqNum) != MMS_UNSIGNED)) {
                if (DEBUG_IED_CLIENT)
                    printf("IED_CLIENT: received malformed report (MoreSegmentsFollow)\n");

                goto exit_function;
            }
        }
        else {
            matchingReport->moreSegementsFollow = MmsValue_getBoolean(moreSegmentsFollow);
        }

        matchingReport->hasSequenceNumber = true;
    }
    else {
        matchingReport->subSeqNum = 0;
        matchingReport->moreSegementsFollow = false;
    }

    MmsValue* inclusion = MmsValue_getElement(value, inclusionIndex);

    if ((inclusion == NULL) || (MmsValue_getType(inclusion) != MMS_BIT_STRING)) {
        if (DEBUG_IED_CLIENT)
            printf("IED_CLIENT: received malformed report (inclusion)\n");

        goto exit_function;
    }

    matchingReport->inclusion = inclusion;
    int dataSetSize = MmsValue_getBitStringSize(inclusion);

    if (matchingReport->dataSetSize == -1) {
        matchingReport->dataSetSize = dataSetSize;
    }
    else {
        if (dataSetSize != matchingReport->dataSetSize) {
            if (DEBUG_IED_CLIENT)
                printf("IED_CLIENT: received malformed report (inclusion has no plausible size)\n");

            goto exit_function;
        }
    }

//     if (DEBUG_IED_CLIENT)
//         printf("IED_CLIENT: Report includes %i data set elements of %i\n", includedElements,
//             dataSetSize);

    matchingReport->valueIndex = inclusionIndex + 1;
    if (MmsValue_getBitStringBit(optFlds, 3)) {
        matchingReport->hasReasonForInclusion = true;
    }

    if (MmsValue_getBitStringBit(optFlds, 5)) {
        matchingReport->hasDataReference = true;
    }

    matchingReport->dataValues = value;

    Semaphore_wait(self->reportHandlerMutex);

    if (matchingReport->callback != NULL)
        matchingReport->callback(matchingReport->callbackParameter, matchingReport);

    Semaphore_post(self->reportHandlerMutex);
    return;

exit_function:
    return;
}
