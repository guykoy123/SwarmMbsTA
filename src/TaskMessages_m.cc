//
// Generated file, do not edit! Created by opp_msgtool 6.3 from src/TaskMessages.msg.
//

// Disable warnings about unused variables, empty switch stmts, etc:
#ifdef _MSC_VER
#  pragma warning(disable:4101)
#  pragma warning(disable:4065)
#endif

#if defined(__clang__)
#  pragma clang diagnostic ignored "-Wshadow"
#  pragma clang diagnostic ignored "-Wconversion"
#  pragma clang diagnostic ignored "-Wunused-parameter"
#  pragma clang diagnostic ignored "-Wc++98-compat"
#  pragma clang diagnostic ignored "-Wunreachable-code-break"
#  pragma clang diagnostic ignored "-Wold-style-cast"
#elif defined(__GNUC__)
#  pragma GCC diagnostic ignored "-Wshadow"
#  pragma GCC diagnostic ignored "-Wconversion"
#  pragma GCC diagnostic ignored "-Wunused-parameter"
#  pragma GCC diagnostic ignored "-Wold-style-cast"
#  pragma GCC diagnostic ignored "-Wsuggest-attribute=noreturn"
#  pragma GCC diagnostic ignored "-Wfloat-conversion"
#endif

#include <iostream>
#include <sstream>
#include <memory>
#include <type_traits>
#include "TaskMessages_m.h"

namespace omnetpp {

// Template pack/unpack rules. They are declared *after* a1l type-specific pack functions for multiple reasons.
// They are in the omnetpp namespace, to allow them to be found by argument-dependent lookup via the cCommBuffer argument

// Packing/unpacking an std::vector
template<typename T, typename A>
void doParsimPacking(omnetpp::cCommBuffer *buffer, const std::vector<T,A>& v)
{
    int n = v.size();
    doParsimPacking(buffer, n);
    for (int i = 0; i < n; i++)
        doParsimPacking(buffer, v[i]);
}

template<typename T, typename A>
void doParsimUnpacking(omnetpp::cCommBuffer *buffer, std::vector<T,A>& v)
{
    int n;
    doParsimUnpacking(buffer, n);
    v.resize(n);
    for (int i = 0; i < n; i++)
        doParsimUnpacking(buffer, v[i]);
}

// Packing/unpacking an std::list
template<typename T, typename A>
void doParsimPacking(omnetpp::cCommBuffer *buffer, const std::list<T,A>& l)
{
    doParsimPacking(buffer, (int)l.size());
    for (typename std::list<T,A>::const_iterator it = l.begin(); it != l.end(); ++it)
        doParsimPacking(buffer, (T&)*it);
}

template<typename T, typename A>
void doParsimUnpacking(omnetpp::cCommBuffer *buffer, std::list<T,A>& l)
{
    int n;
    doParsimUnpacking(buffer, n);
    for (int i = 0; i < n; i++) {
        l.push_back(T());
        doParsimUnpacking(buffer, l.back());
    }
}

// Packing/unpacking an std::set
template<typename T, typename Tr, typename A>
void doParsimPacking(omnetpp::cCommBuffer *buffer, const std::set<T,Tr,A>& s)
{
    doParsimPacking(buffer, (int)s.size());
    for (typename std::set<T,Tr,A>::const_iterator it = s.begin(); it != s.end(); ++it)
        doParsimPacking(buffer, *it);
}

template<typename T, typename Tr, typename A>
void doParsimUnpacking(omnetpp::cCommBuffer *buffer, std::set<T,Tr,A>& s)
{
    int n;
    doParsimUnpacking(buffer, n);
    for (int i = 0; i < n; i++) {
        T x;
        doParsimUnpacking(buffer, x);
        s.insert(x);
    }
}

// Packing/unpacking an std::map
template<typename K, typename V, typename Tr, typename A>
void doParsimPacking(omnetpp::cCommBuffer *buffer, const std::map<K,V,Tr,A>& m)
{
    doParsimPacking(buffer, (int)m.size());
    for (typename std::map<K,V,Tr,A>::const_iterator it = m.begin(); it != m.end(); ++it) {
        doParsimPacking(buffer, it->first);
        doParsimPacking(buffer, it->second);
    }
}

template<typename K, typename V, typename Tr, typename A>
void doParsimUnpacking(omnetpp::cCommBuffer *buffer, std::map<K,V,Tr,A>& m)
{
    int n;
    doParsimUnpacking(buffer, n);
    for (int i = 0; i < n; i++) {
        K k; V v;
        doParsimUnpacking(buffer, k);
        doParsimUnpacking(buffer, v);
        m[k] = v;
    }
}

// Default pack/unpack function for arrays
template<typename T>
void doParsimArrayPacking(omnetpp::cCommBuffer *b, const T *t, int n)
{
    for (int i = 0; i < n; i++)
        doParsimPacking(b, t[i]);
}

template<typename T>
void doParsimArrayUnpacking(omnetpp::cCommBuffer *b, T *t, int n)
{
    for (int i = 0; i < n; i++)
        doParsimUnpacking(b, t[i]);
}

// Default rule to prevent compiler from choosing base class' doParsimPacking() function
template<typename T>
void doParsimPacking(omnetpp::cCommBuffer *, const T& t)
{
    throw omnetpp::cRuntimeError("Parsim error: No doParsimPacking() function for type %s", omnetpp::opp_typename(typeid(t)));
}

template<typename T>
void doParsimUnpacking(omnetpp::cCommBuffer *, T& t)
{
    throw omnetpp::cRuntimeError("Parsim error: No doParsimUnpacking() function for type %s", omnetpp::opp_typename(typeid(t)));
}

}  // namespace omnetpp

namespace uavswarmta {

Register_Class(TaskNotification)

TaskNotification::TaskNotification() : ::inet::FieldsChunk()
{
}

TaskNotification::TaskNotification(const TaskNotification& other) : ::inet::FieldsChunk(other)
{
    copy(other);
}

TaskNotification::~TaskNotification()
{
}

TaskNotification& TaskNotification::operator=(const TaskNotification& other)
{
    if (this == &other) return *this;
    ::inet::FieldsChunk::operator=(other);
    copy(other);
    return *this;
}

void TaskNotification::copy(const TaskNotification& other)
{
    this->taskId = other.taskId;
    this->targetX = other.targetX;
    this->targetY = other.targetY;
    this->priority = other.priority;
    this->requiredDrones = other.requiredDrones;
    this->duration = other.duration;
}

void TaskNotification::parsimPack(omnetpp::cCommBuffer *b) const
{
    ::inet::FieldsChunk::parsimPack(b);
    doParsimPacking(b,this->taskId);
    doParsimPacking(b,this->targetX);
    doParsimPacking(b,this->targetY);
    doParsimPacking(b,this->priority);
    doParsimPacking(b,this->requiredDrones);
    doParsimPacking(b,this->duration);
}

void TaskNotification::parsimUnpack(omnetpp::cCommBuffer *b)
{
    ::inet::FieldsChunk::parsimUnpack(b);
    doParsimUnpacking(b,this->taskId);
    doParsimUnpacking(b,this->targetX);
    doParsimUnpacking(b,this->targetY);
    doParsimUnpacking(b,this->priority);
    doParsimUnpacking(b,this->requiredDrones);
    doParsimUnpacking(b,this->duration);
}

int TaskNotification::getTaskId() const
{
    return this->taskId;
}

void TaskNotification::setTaskId(int taskId)
{
    handleChange();
    this->taskId = taskId;
}

double TaskNotification::getTargetX() const
{
    return this->targetX;
}

void TaskNotification::setTargetX(double targetX)
{
    handleChange();
    this->targetX = targetX;
}

double TaskNotification::getTargetY() const
{
    return this->targetY;
}

void TaskNotification::setTargetY(double targetY)
{
    handleChange();
    this->targetY = targetY;
}

int TaskNotification::getPriority() const
{
    return this->priority;
}

void TaskNotification::setPriority(int priority)
{
    handleChange();
    this->priority = priority;
}

int TaskNotification::getRequiredDrones() const
{
    return this->requiredDrones;
}

void TaskNotification::setRequiredDrones(int requiredDrones)
{
    handleChange();
    this->requiredDrones = requiredDrones;
}

double TaskNotification::getDuration() const
{
    return this->duration;
}

void TaskNotification::setDuration(double duration)
{
    handleChange();
    this->duration = duration;
}

class TaskNotificationDescriptor : public omnetpp::cClassDescriptor
{
  private:
    mutable const char **propertyNames;
    enum FieldConstants {
        FIELD_taskId,
        FIELD_targetX,
        FIELD_targetY,
        FIELD_priority,
        FIELD_requiredDrones,
        FIELD_duration,
    };
  public:
    TaskNotificationDescriptor();
    virtual ~TaskNotificationDescriptor();

    virtual bool doesSupport(omnetpp::cObject *obj) const override;
    virtual const char **getPropertyNames() const override;
    virtual const char *getProperty(const char *propertyName) const override;
    virtual int getFieldCount() const override;
    virtual const char *getFieldName(int field) const override;
    virtual int findField(const char *fieldName) const override;
    virtual unsigned int getFieldTypeFlags(int field) const override;
    virtual const char *getFieldTypeString(int field) const override;
    virtual const char **getFieldPropertyNames(int field) const override;
    virtual const char *getFieldProperty(int field, const char *propertyName) const override;
    virtual int getFieldArraySize(omnetpp::any_ptr object, int field) const override;
    virtual void setFieldArraySize(omnetpp::any_ptr object, int field, int size) const override;

    virtual const char *getFieldDynamicTypeString(omnetpp::any_ptr object, int field, int i) const override;
    virtual std::string getFieldValueAsString(omnetpp::any_ptr object, int field, int i) const override;
    virtual void setFieldValueAsString(omnetpp::any_ptr object, int field, int i, const char *value) const override;
    virtual omnetpp::cValue getFieldValue(omnetpp::any_ptr object, int field, int i) const override;
    virtual void setFieldValue(omnetpp::any_ptr object, int field, int i, const omnetpp::cValue& value) const override;

    virtual const char *getFieldStructName(int field) const override;
    virtual omnetpp::any_ptr getFieldStructValuePointer(omnetpp::any_ptr object, int field, int i) const override;
    virtual void setFieldStructValuePointer(omnetpp::any_ptr object, int field, int i, omnetpp::any_ptr ptr) const override;
};

Register_ClassDescriptor(TaskNotificationDescriptor)

TaskNotificationDescriptor::TaskNotificationDescriptor() : omnetpp::cClassDescriptor(omnetpp::opp_typename(typeid(uavswarmta::TaskNotification)), "inet::FieldsChunk")
{
    propertyNames = nullptr;
}

TaskNotificationDescriptor::~TaskNotificationDescriptor()
{
    delete[] propertyNames;
}

bool TaskNotificationDescriptor::doesSupport(omnetpp::cObject *obj) const
{
    return dynamic_cast<TaskNotification *>(obj)!=nullptr;
}

const char **TaskNotificationDescriptor::getPropertyNames() const
{
    if (!propertyNames) {
        static const char *names[] = {  nullptr };
        omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
        const char **baseNames = base ? base->getPropertyNames() : nullptr;
        propertyNames = mergeLists(baseNames, names);
    }
    return propertyNames;
}

const char *TaskNotificationDescriptor::getProperty(const char *propertyName) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    return base ? base->getProperty(propertyName) : nullptr;
}

int TaskNotificationDescriptor::getFieldCount() const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    return base ? 6+base->getFieldCount() : 6;
}

unsigned int TaskNotificationDescriptor::getFieldTypeFlags(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldTypeFlags(field);
        field -= base->getFieldCount();
    }
    static unsigned int fieldTypeFlags[] = {
        FD_ISEDITABLE,    // FIELD_taskId
        FD_ISEDITABLE,    // FIELD_targetX
        FD_ISEDITABLE,    // FIELD_targetY
        FD_ISEDITABLE,    // FIELD_priority
        FD_ISEDITABLE,    // FIELD_requiredDrones
        FD_ISEDITABLE,    // FIELD_duration
    };
    return (field >= 0 && field < 6) ? fieldTypeFlags[field] : 0;
}

const char *TaskNotificationDescriptor::getFieldName(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldName(field);
        field -= base->getFieldCount();
    }
    static const char *fieldNames[] = {
        "taskId",
        "targetX",
        "targetY",
        "priority",
        "requiredDrones",
        "duration",
    };
    return (field >= 0 && field < 6) ? fieldNames[field] : nullptr;
}

int TaskNotificationDescriptor::findField(const char *fieldName) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    int baseIndex = base ? base->getFieldCount() : 0;
    if (strcmp(fieldName, "taskId") == 0) return baseIndex + 0;
    if (strcmp(fieldName, "targetX") == 0) return baseIndex + 1;
    if (strcmp(fieldName, "targetY") == 0) return baseIndex + 2;
    if (strcmp(fieldName, "priority") == 0) return baseIndex + 3;
    if (strcmp(fieldName, "requiredDrones") == 0) return baseIndex + 4;
    if (strcmp(fieldName, "duration") == 0) return baseIndex + 5;
    return base ? base->findField(fieldName) : -1;
}

const char *TaskNotificationDescriptor::getFieldTypeString(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldTypeString(field);
        field -= base->getFieldCount();
    }
    static const char *fieldTypeStrings[] = {
        "int",    // FIELD_taskId
        "double",    // FIELD_targetX
        "double",    // FIELD_targetY
        "int",    // FIELD_priority
        "int",    // FIELD_requiredDrones
        "double",    // FIELD_duration
    };
    return (field >= 0 && field < 6) ? fieldTypeStrings[field] : nullptr;
}

const char **TaskNotificationDescriptor::getFieldPropertyNames(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldPropertyNames(field);
        field -= base->getFieldCount();
    }
    switch (field) {
        default: return nullptr;
    }
}

const char *TaskNotificationDescriptor::getFieldProperty(int field, const char *propertyName) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldProperty(field, propertyName);
        field -= base->getFieldCount();
    }
    switch (field) {
        default: return nullptr;
    }
}

int TaskNotificationDescriptor::getFieldArraySize(omnetpp::any_ptr object, int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldArraySize(object, field);
        field -= base->getFieldCount();
    }
    TaskNotification *pp = omnetpp::fromAnyPtr<TaskNotification>(object); (void)pp;
    switch (field) {
        default: return 0;
    }
}

void TaskNotificationDescriptor::setFieldArraySize(omnetpp::any_ptr object, int field, int size) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldArraySize(object, field, size);
            return;
        }
        field -= base->getFieldCount();
    }
    TaskNotification *pp = omnetpp::fromAnyPtr<TaskNotification>(object); (void)pp;
    switch (field) {
        default: throw omnetpp::cRuntimeError("Cannot set array size of field %d of class 'TaskNotification'", field);
    }
}

const char *TaskNotificationDescriptor::getFieldDynamicTypeString(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldDynamicTypeString(object,field,i);
        field -= base->getFieldCount();
    }
    TaskNotification *pp = omnetpp::fromAnyPtr<TaskNotification>(object); (void)pp;
    switch (field) {
        default: return nullptr;
    }
}

std::string TaskNotificationDescriptor::getFieldValueAsString(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldValueAsString(object,field,i);
        field -= base->getFieldCount();
    }
    TaskNotification *pp = omnetpp::fromAnyPtr<TaskNotification>(object); (void)pp;
    switch (field) {
        case FIELD_taskId: return long2string(pp->getTaskId());
        case FIELD_targetX: return double2string(pp->getTargetX());
        case FIELD_targetY: return double2string(pp->getTargetY());
        case FIELD_priority: return long2string(pp->getPriority());
        case FIELD_requiredDrones: return long2string(pp->getRequiredDrones());
        case FIELD_duration: return double2string(pp->getDuration());
        default: return "";
    }
}

void TaskNotificationDescriptor::setFieldValueAsString(omnetpp::any_ptr object, int field, int i, const char *value) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldValueAsString(object, field, i, value);
            return;
        }
        field -= base->getFieldCount();
    }
    TaskNotification *pp = omnetpp::fromAnyPtr<TaskNotification>(object); (void)pp;
    switch (field) {
        case FIELD_taskId: pp->setTaskId(string2long(value)); break;
        case FIELD_targetX: pp->setTargetX(string2double(value)); break;
        case FIELD_targetY: pp->setTargetY(string2double(value)); break;
        case FIELD_priority: pp->setPriority(string2long(value)); break;
        case FIELD_requiredDrones: pp->setRequiredDrones(string2long(value)); break;
        case FIELD_duration: pp->setDuration(string2double(value)); break;
        default: throw omnetpp::cRuntimeError("Cannot set field %d of class 'TaskNotification'", field);
    }
}

omnetpp::cValue TaskNotificationDescriptor::getFieldValue(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldValue(object,field,i);
        field -= base->getFieldCount();
    }
    TaskNotification *pp = omnetpp::fromAnyPtr<TaskNotification>(object); (void)pp;
    switch (field) {
        case FIELD_taskId: return pp->getTaskId();
        case FIELD_targetX: return pp->getTargetX();
        case FIELD_targetY: return pp->getTargetY();
        case FIELD_priority: return pp->getPriority();
        case FIELD_requiredDrones: return pp->getRequiredDrones();
        case FIELD_duration: return pp->getDuration();
        default: throw omnetpp::cRuntimeError("Cannot return field %d of class 'TaskNotification' as cValue -- field index out of range?", field);
    }
}

void TaskNotificationDescriptor::setFieldValue(omnetpp::any_ptr object, int field, int i, const omnetpp::cValue& value) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldValue(object, field, i, value);
            return;
        }
        field -= base->getFieldCount();
    }
    TaskNotification *pp = omnetpp::fromAnyPtr<TaskNotification>(object); (void)pp;
    switch (field) {
        case FIELD_taskId: pp->setTaskId(omnetpp::checked_int_cast<int>(value.intValue())); break;
        case FIELD_targetX: pp->setTargetX(value.doubleValue()); break;
        case FIELD_targetY: pp->setTargetY(value.doubleValue()); break;
        case FIELD_priority: pp->setPriority(omnetpp::checked_int_cast<int>(value.intValue())); break;
        case FIELD_requiredDrones: pp->setRequiredDrones(omnetpp::checked_int_cast<int>(value.intValue())); break;
        case FIELD_duration: pp->setDuration(value.doubleValue()); break;
        default: throw omnetpp::cRuntimeError("Cannot set field %d of class 'TaskNotification'", field);
    }
}

const char *TaskNotificationDescriptor::getFieldStructName(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldStructName(field);
        field -= base->getFieldCount();
    }
    switch (field) {
        default: return nullptr;
    };
}

omnetpp::any_ptr TaskNotificationDescriptor::getFieldStructValuePointer(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldStructValuePointer(object, field, i);
        field -= base->getFieldCount();
    }
    TaskNotification *pp = omnetpp::fromAnyPtr<TaskNotification>(object); (void)pp;
    switch (field) {
        default: return omnetpp::any_ptr(nullptr);
    }
}

void TaskNotificationDescriptor::setFieldStructValuePointer(omnetpp::any_ptr object, int field, int i, omnetpp::any_ptr ptr) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldStructValuePointer(object, field, i, ptr);
            return;
        }
        field -= base->getFieldCount();
    }
    TaskNotification *pp = omnetpp::fromAnyPtr<TaskNotification>(object); (void)pp;
    switch (field) {
        default: throw omnetpp::cRuntimeError("Cannot set field %d of class 'TaskNotification'", field);
    }
}

Register_Class(TaskBid)

TaskBid::TaskBid() : ::inet::FieldsChunk()
{
}

TaskBid::TaskBid(const TaskBid& other) : ::inet::FieldsChunk(other)
{
    copy(other);
}

TaskBid::~TaskBid()
{
}

TaskBid& TaskBid::operator=(const TaskBid& other)
{
    if (this == &other) return *this;
    ::inet::FieldsChunk::operator=(other);
    copy(other);
    return *this;
}

void TaskBid::copy(const TaskBid& other)
{
    this->taskId = other.taskId;
    this->droneId = other.droneId;
    this->bidValue = other.bidValue;
}

void TaskBid::parsimPack(omnetpp::cCommBuffer *b) const
{
    ::inet::FieldsChunk::parsimPack(b);
    doParsimPacking(b,this->taskId);
    doParsimPacking(b,this->droneId);
    doParsimPacking(b,this->bidValue);
}

void TaskBid::parsimUnpack(omnetpp::cCommBuffer *b)
{
    ::inet::FieldsChunk::parsimUnpack(b);
    doParsimUnpacking(b,this->taskId);
    doParsimUnpacking(b,this->droneId);
    doParsimUnpacking(b,this->bidValue);
}

int TaskBid::getTaskId() const
{
    return this->taskId;
}

void TaskBid::setTaskId(int taskId)
{
    handleChange();
    this->taskId = taskId;
}

int TaskBid::getDroneId() const
{
    return this->droneId;
}

void TaskBid::setDroneId(int droneId)
{
    handleChange();
    this->droneId = droneId;
}

double TaskBid::getBidValue() const
{
    return this->bidValue;
}

void TaskBid::setBidValue(double bidValue)
{
    handleChange();
    this->bidValue = bidValue;
}

class TaskBidDescriptor : public omnetpp::cClassDescriptor
{
  private:
    mutable const char **propertyNames;
    enum FieldConstants {
        FIELD_taskId,
        FIELD_droneId,
        FIELD_bidValue,
    };
  public:
    TaskBidDescriptor();
    virtual ~TaskBidDescriptor();

    virtual bool doesSupport(omnetpp::cObject *obj) const override;
    virtual const char **getPropertyNames() const override;
    virtual const char *getProperty(const char *propertyName) const override;
    virtual int getFieldCount() const override;
    virtual const char *getFieldName(int field) const override;
    virtual int findField(const char *fieldName) const override;
    virtual unsigned int getFieldTypeFlags(int field) const override;
    virtual const char *getFieldTypeString(int field) const override;
    virtual const char **getFieldPropertyNames(int field) const override;
    virtual const char *getFieldProperty(int field, const char *propertyName) const override;
    virtual int getFieldArraySize(omnetpp::any_ptr object, int field) const override;
    virtual void setFieldArraySize(omnetpp::any_ptr object, int field, int size) const override;

    virtual const char *getFieldDynamicTypeString(omnetpp::any_ptr object, int field, int i) const override;
    virtual std::string getFieldValueAsString(omnetpp::any_ptr object, int field, int i) const override;
    virtual void setFieldValueAsString(omnetpp::any_ptr object, int field, int i, const char *value) const override;
    virtual omnetpp::cValue getFieldValue(omnetpp::any_ptr object, int field, int i) const override;
    virtual void setFieldValue(omnetpp::any_ptr object, int field, int i, const omnetpp::cValue& value) const override;

    virtual const char *getFieldStructName(int field) const override;
    virtual omnetpp::any_ptr getFieldStructValuePointer(omnetpp::any_ptr object, int field, int i) const override;
    virtual void setFieldStructValuePointer(omnetpp::any_ptr object, int field, int i, omnetpp::any_ptr ptr) const override;
};

Register_ClassDescriptor(TaskBidDescriptor)

TaskBidDescriptor::TaskBidDescriptor() : omnetpp::cClassDescriptor(omnetpp::opp_typename(typeid(uavswarmta::TaskBid)), "inet::FieldsChunk")
{
    propertyNames = nullptr;
}

TaskBidDescriptor::~TaskBidDescriptor()
{
    delete[] propertyNames;
}

bool TaskBidDescriptor::doesSupport(omnetpp::cObject *obj) const
{
    return dynamic_cast<TaskBid *>(obj)!=nullptr;
}

const char **TaskBidDescriptor::getPropertyNames() const
{
    if (!propertyNames) {
        static const char *names[] = {  nullptr };
        omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
        const char **baseNames = base ? base->getPropertyNames() : nullptr;
        propertyNames = mergeLists(baseNames, names);
    }
    return propertyNames;
}

const char *TaskBidDescriptor::getProperty(const char *propertyName) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    return base ? base->getProperty(propertyName) : nullptr;
}

int TaskBidDescriptor::getFieldCount() const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    return base ? 3+base->getFieldCount() : 3;
}

unsigned int TaskBidDescriptor::getFieldTypeFlags(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldTypeFlags(field);
        field -= base->getFieldCount();
    }
    static unsigned int fieldTypeFlags[] = {
        FD_ISEDITABLE,    // FIELD_taskId
        FD_ISEDITABLE,    // FIELD_droneId
        FD_ISEDITABLE,    // FIELD_bidValue
    };
    return (field >= 0 && field < 3) ? fieldTypeFlags[field] : 0;
}

const char *TaskBidDescriptor::getFieldName(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldName(field);
        field -= base->getFieldCount();
    }
    static const char *fieldNames[] = {
        "taskId",
        "droneId",
        "bidValue",
    };
    return (field >= 0 && field < 3) ? fieldNames[field] : nullptr;
}

int TaskBidDescriptor::findField(const char *fieldName) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    int baseIndex = base ? base->getFieldCount() : 0;
    if (strcmp(fieldName, "taskId") == 0) return baseIndex + 0;
    if (strcmp(fieldName, "droneId") == 0) return baseIndex + 1;
    if (strcmp(fieldName, "bidValue") == 0) return baseIndex + 2;
    return base ? base->findField(fieldName) : -1;
}

const char *TaskBidDescriptor::getFieldTypeString(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldTypeString(field);
        field -= base->getFieldCount();
    }
    static const char *fieldTypeStrings[] = {
        "int",    // FIELD_taskId
        "int",    // FIELD_droneId
        "double",    // FIELD_bidValue
    };
    return (field >= 0 && field < 3) ? fieldTypeStrings[field] : nullptr;
}

const char **TaskBidDescriptor::getFieldPropertyNames(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldPropertyNames(field);
        field -= base->getFieldCount();
    }
    switch (field) {
        default: return nullptr;
    }
}

const char *TaskBidDescriptor::getFieldProperty(int field, const char *propertyName) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldProperty(field, propertyName);
        field -= base->getFieldCount();
    }
    switch (field) {
        default: return nullptr;
    }
}

int TaskBidDescriptor::getFieldArraySize(omnetpp::any_ptr object, int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldArraySize(object, field);
        field -= base->getFieldCount();
    }
    TaskBid *pp = omnetpp::fromAnyPtr<TaskBid>(object); (void)pp;
    switch (field) {
        default: return 0;
    }
}

void TaskBidDescriptor::setFieldArraySize(omnetpp::any_ptr object, int field, int size) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldArraySize(object, field, size);
            return;
        }
        field -= base->getFieldCount();
    }
    TaskBid *pp = omnetpp::fromAnyPtr<TaskBid>(object); (void)pp;
    switch (field) {
        default: throw omnetpp::cRuntimeError("Cannot set array size of field %d of class 'TaskBid'", field);
    }
}

const char *TaskBidDescriptor::getFieldDynamicTypeString(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldDynamicTypeString(object,field,i);
        field -= base->getFieldCount();
    }
    TaskBid *pp = omnetpp::fromAnyPtr<TaskBid>(object); (void)pp;
    switch (field) {
        default: return nullptr;
    }
}

std::string TaskBidDescriptor::getFieldValueAsString(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldValueAsString(object,field,i);
        field -= base->getFieldCount();
    }
    TaskBid *pp = omnetpp::fromAnyPtr<TaskBid>(object); (void)pp;
    switch (field) {
        case FIELD_taskId: return long2string(pp->getTaskId());
        case FIELD_droneId: return long2string(pp->getDroneId());
        case FIELD_bidValue: return double2string(pp->getBidValue());
        default: return "";
    }
}

void TaskBidDescriptor::setFieldValueAsString(omnetpp::any_ptr object, int field, int i, const char *value) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldValueAsString(object, field, i, value);
            return;
        }
        field -= base->getFieldCount();
    }
    TaskBid *pp = omnetpp::fromAnyPtr<TaskBid>(object); (void)pp;
    switch (field) {
        case FIELD_taskId: pp->setTaskId(string2long(value)); break;
        case FIELD_droneId: pp->setDroneId(string2long(value)); break;
        case FIELD_bidValue: pp->setBidValue(string2double(value)); break;
        default: throw omnetpp::cRuntimeError("Cannot set field %d of class 'TaskBid'", field);
    }
}

omnetpp::cValue TaskBidDescriptor::getFieldValue(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldValue(object,field,i);
        field -= base->getFieldCount();
    }
    TaskBid *pp = omnetpp::fromAnyPtr<TaskBid>(object); (void)pp;
    switch (field) {
        case FIELD_taskId: return pp->getTaskId();
        case FIELD_droneId: return pp->getDroneId();
        case FIELD_bidValue: return pp->getBidValue();
        default: throw omnetpp::cRuntimeError("Cannot return field %d of class 'TaskBid' as cValue -- field index out of range?", field);
    }
}

void TaskBidDescriptor::setFieldValue(omnetpp::any_ptr object, int field, int i, const omnetpp::cValue& value) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldValue(object, field, i, value);
            return;
        }
        field -= base->getFieldCount();
    }
    TaskBid *pp = omnetpp::fromAnyPtr<TaskBid>(object); (void)pp;
    switch (field) {
        case FIELD_taskId: pp->setTaskId(omnetpp::checked_int_cast<int>(value.intValue())); break;
        case FIELD_droneId: pp->setDroneId(omnetpp::checked_int_cast<int>(value.intValue())); break;
        case FIELD_bidValue: pp->setBidValue(value.doubleValue()); break;
        default: throw omnetpp::cRuntimeError("Cannot set field %d of class 'TaskBid'", field);
    }
}

const char *TaskBidDescriptor::getFieldStructName(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldStructName(field);
        field -= base->getFieldCount();
    }
    switch (field) {
        default: return nullptr;
    };
}

omnetpp::any_ptr TaskBidDescriptor::getFieldStructValuePointer(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldStructValuePointer(object, field, i);
        field -= base->getFieldCount();
    }
    TaskBid *pp = omnetpp::fromAnyPtr<TaskBid>(object); (void)pp;
    switch (field) {
        default: return omnetpp::any_ptr(nullptr);
    }
}

void TaskBidDescriptor::setFieldStructValuePointer(omnetpp::any_ptr object, int field, int i, omnetpp::any_ptr ptr) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldStructValuePointer(object, field, i, ptr);
            return;
        }
        field -= base->getFieldCount();
    }
    TaskBid *pp = omnetpp::fromAnyPtr<TaskBid>(object); (void)pp;
    switch (field) {
        default: throw omnetpp::cRuntimeError("Cannot set field %d of class 'TaskBid'", field);
    }
}

Register_Class(TaskAssignment)

TaskAssignment::TaskAssignment() : ::inet::FieldsChunk()
{
}

TaskAssignment::TaskAssignment(const TaskAssignment& other) : ::inet::FieldsChunk(other)
{
    copy(other);
}

TaskAssignment::~TaskAssignment()
{
    delete [] this->assignedDroneIds;
}

TaskAssignment& TaskAssignment::operator=(const TaskAssignment& other)
{
    if (this == &other) return *this;
    ::inet::FieldsChunk::operator=(other);
    copy(other);
    return *this;
}

void TaskAssignment::copy(const TaskAssignment& other)
{
    this->taskId = other.taskId;
    delete [] this->assignedDroneIds;
    this->assignedDroneIds = (other.assignedDroneIds_arraysize==0) ? nullptr : new int[other.assignedDroneIds_arraysize];
    assignedDroneIds_arraysize = other.assignedDroneIds_arraysize;
    for (size_t i = 0; i < assignedDroneIds_arraysize; i++) {
        this->assignedDroneIds[i] = other.assignedDroneIds[i];
    }
}

void TaskAssignment::parsimPack(omnetpp::cCommBuffer *b) const
{
    ::inet::FieldsChunk::parsimPack(b);
    doParsimPacking(b,this->taskId);
    b->pack(assignedDroneIds_arraysize);
    doParsimArrayPacking(b,this->assignedDroneIds,assignedDroneIds_arraysize);
}

void TaskAssignment::parsimUnpack(omnetpp::cCommBuffer *b)
{
    ::inet::FieldsChunk::parsimUnpack(b);
    doParsimUnpacking(b,this->taskId);
    delete [] this->assignedDroneIds;
    b->unpack(assignedDroneIds_arraysize);
    if (assignedDroneIds_arraysize == 0) {
        this->assignedDroneIds = nullptr;
    } else {
        this->assignedDroneIds = new int[assignedDroneIds_arraysize];
        doParsimArrayUnpacking(b,this->assignedDroneIds,assignedDroneIds_arraysize);
    }
}

int TaskAssignment::getTaskId() const
{
    return this->taskId;
}

void TaskAssignment::setTaskId(int taskId)
{
    handleChange();
    this->taskId = taskId;
}

size_t TaskAssignment::getAssignedDroneIdsArraySize() const
{
    return assignedDroneIds_arraysize;
}

int TaskAssignment::getAssignedDroneIds(size_t k) const
{
    if (k >= assignedDroneIds_arraysize) throw omnetpp::cRuntimeError("Array of size %lu indexed by %lu", (unsigned long)assignedDroneIds_arraysize, (unsigned long)k);
    return this->assignedDroneIds[k];
}

void TaskAssignment::setAssignedDroneIdsArraySize(size_t newSize)
{
    handleChange();
    int *assignedDroneIds2 = (newSize==0) ? nullptr : new int[newSize];
    size_t minSize = assignedDroneIds_arraysize < newSize ? assignedDroneIds_arraysize : newSize;
    for (size_t i = 0; i < minSize; i++)
        assignedDroneIds2[i] = this->assignedDroneIds[i];
    for (size_t i = minSize; i < newSize; i++)
        assignedDroneIds2[i] = 0;
    delete [] this->assignedDroneIds;
    this->assignedDroneIds = assignedDroneIds2;
    assignedDroneIds_arraysize = newSize;
}

void TaskAssignment::setAssignedDroneIds(size_t k, int assignedDroneIds)
{
    if (k >= assignedDroneIds_arraysize) throw omnetpp::cRuntimeError("Array of size %lu indexed by %lu", (unsigned long)assignedDroneIds_arraysize, (unsigned long)k);
    handleChange();
    this->assignedDroneIds[k] = assignedDroneIds;
}

void TaskAssignment::insertAssignedDroneIds(size_t k, int assignedDroneIds)
{
    if (k > assignedDroneIds_arraysize) throw omnetpp::cRuntimeError("Array of size %lu indexed by %lu", (unsigned long)assignedDroneIds_arraysize, (unsigned long)k);
    handleChange();
    size_t newSize = assignedDroneIds_arraysize + 1;
    int *assignedDroneIds2 = new int[newSize];
    size_t i;
    for (i = 0; i < k; i++)
        assignedDroneIds2[i] = this->assignedDroneIds[i];
    assignedDroneIds2[k] = assignedDroneIds;
    for (i = k + 1; i < newSize; i++)
        assignedDroneIds2[i] = this->assignedDroneIds[i-1];
    delete [] this->assignedDroneIds;
    this->assignedDroneIds = assignedDroneIds2;
    assignedDroneIds_arraysize = newSize;
}

void TaskAssignment::appendAssignedDroneIds(int assignedDroneIds)
{
    insertAssignedDroneIds(assignedDroneIds_arraysize, assignedDroneIds);
}

void TaskAssignment::eraseAssignedDroneIds(size_t k)
{
    if (k >= assignedDroneIds_arraysize) throw omnetpp::cRuntimeError("Array of size %lu indexed by %lu", (unsigned long)assignedDroneIds_arraysize, (unsigned long)k);
    handleChange();
    size_t newSize = assignedDroneIds_arraysize - 1;
    int *assignedDroneIds2 = (newSize == 0) ? nullptr : new int[newSize];
    size_t i;
    for (i = 0; i < k; i++)
        assignedDroneIds2[i] = this->assignedDroneIds[i];
    for (i = k; i < newSize; i++)
        assignedDroneIds2[i] = this->assignedDroneIds[i+1];
    delete [] this->assignedDroneIds;
    this->assignedDroneIds = assignedDroneIds2;
    assignedDroneIds_arraysize = newSize;
}

class TaskAssignmentDescriptor : public omnetpp::cClassDescriptor
{
  private:
    mutable const char **propertyNames;
    enum FieldConstants {
        FIELD_taskId,
        FIELD_assignedDroneIds,
    };
  public:
    TaskAssignmentDescriptor();
    virtual ~TaskAssignmentDescriptor();

    virtual bool doesSupport(omnetpp::cObject *obj) const override;
    virtual const char **getPropertyNames() const override;
    virtual const char *getProperty(const char *propertyName) const override;
    virtual int getFieldCount() const override;
    virtual const char *getFieldName(int field) const override;
    virtual int findField(const char *fieldName) const override;
    virtual unsigned int getFieldTypeFlags(int field) const override;
    virtual const char *getFieldTypeString(int field) const override;
    virtual const char **getFieldPropertyNames(int field) const override;
    virtual const char *getFieldProperty(int field, const char *propertyName) const override;
    virtual int getFieldArraySize(omnetpp::any_ptr object, int field) const override;
    virtual void setFieldArraySize(omnetpp::any_ptr object, int field, int size) const override;

    virtual const char *getFieldDynamicTypeString(omnetpp::any_ptr object, int field, int i) const override;
    virtual std::string getFieldValueAsString(omnetpp::any_ptr object, int field, int i) const override;
    virtual void setFieldValueAsString(omnetpp::any_ptr object, int field, int i, const char *value) const override;
    virtual omnetpp::cValue getFieldValue(omnetpp::any_ptr object, int field, int i) const override;
    virtual void setFieldValue(omnetpp::any_ptr object, int field, int i, const omnetpp::cValue& value) const override;

    virtual const char *getFieldStructName(int field) const override;
    virtual omnetpp::any_ptr getFieldStructValuePointer(omnetpp::any_ptr object, int field, int i) const override;
    virtual void setFieldStructValuePointer(omnetpp::any_ptr object, int field, int i, omnetpp::any_ptr ptr) const override;
};

Register_ClassDescriptor(TaskAssignmentDescriptor)

TaskAssignmentDescriptor::TaskAssignmentDescriptor() : omnetpp::cClassDescriptor(omnetpp::opp_typename(typeid(uavswarmta::TaskAssignment)), "inet::FieldsChunk")
{
    propertyNames = nullptr;
}

TaskAssignmentDescriptor::~TaskAssignmentDescriptor()
{
    delete[] propertyNames;
}

bool TaskAssignmentDescriptor::doesSupport(omnetpp::cObject *obj) const
{
    return dynamic_cast<TaskAssignment *>(obj)!=nullptr;
}

const char **TaskAssignmentDescriptor::getPropertyNames() const
{
    if (!propertyNames) {
        static const char *names[] = {  nullptr };
        omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
        const char **baseNames = base ? base->getPropertyNames() : nullptr;
        propertyNames = mergeLists(baseNames, names);
    }
    return propertyNames;
}

const char *TaskAssignmentDescriptor::getProperty(const char *propertyName) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    return base ? base->getProperty(propertyName) : nullptr;
}

int TaskAssignmentDescriptor::getFieldCount() const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    return base ? 2+base->getFieldCount() : 2;
}

unsigned int TaskAssignmentDescriptor::getFieldTypeFlags(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldTypeFlags(field);
        field -= base->getFieldCount();
    }
    static unsigned int fieldTypeFlags[] = {
        FD_ISEDITABLE,    // FIELD_taskId
        FD_ISARRAY | FD_ISEDITABLE | FD_ISRESIZABLE,    // FIELD_assignedDroneIds
    };
    return (field >= 0 && field < 2) ? fieldTypeFlags[field] : 0;
}

const char *TaskAssignmentDescriptor::getFieldName(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldName(field);
        field -= base->getFieldCount();
    }
    static const char *fieldNames[] = {
        "taskId",
        "assignedDroneIds",
    };
    return (field >= 0 && field < 2) ? fieldNames[field] : nullptr;
}

int TaskAssignmentDescriptor::findField(const char *fieldName) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    int baseIndex = base ? base->getFieldCount() : 0;
    if (strcmp(fieldName, "taskId") == 0) return baseIndex + 0;
    if (strcmp(fieldName, "assignedDroneIds") == 0) return baseIndex + 1;
    return base ? base->findField(fieldName) : -1;
}

const char *TaskAssignmentDescriptor::getFieldTypeString(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldTypeString(field);
        field -= base->getFieldCount();
    }
    static const char *fieldTypeStrings[] = {
        "int",    // FIELD_taskId
        "int",    // FIELD_assignedDroneIds
    };
    return (field >= 0 && field < 2) ? fieldTypeStrings[field] : nullptr;
}

const char **TaskAssignmentDescriptor::getFieldPropertyNames(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldPropertyNames(field);
        field -= base->getFieldCount();
    }
    switch (field) {
        default: return nullptr;
    }
}

const char *TaskAssignmentDescriptor::getFieldProperty(int field, const char *propertyName) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldProperty(field, propertyName);
        field -= base->getFieldCount();
    }
    switch (field) {
        default: return nullptr;
    }
}

int TaskAssignmentDescriptor::getFieldArraySize(omnetpp::any_ptr object, int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldArraySize(object, field);
        field -= base->getFieldCount();
    }
    TaskAssignment *pp = omnetpp::fromAnyPtr<TaskAssignment>(object); (void)pp;
    switch (field) {
        case FIELD_assignedDroneIds: return pp->getAssignedDroneIdsArraySize();
        default: return 0;
    }
}

void TaskAssignmentDescriptor::setFieldArraySize(omnetpp::any_ptr object, int field, int size) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldArraySize(object, field, size);
            return;
        }
        field -= base->getFieldCount();
    }
    TaskAssignment *pp = omnetpp::fromAnyPtr<TaskAssignment>(object); (void)pp;
    switch (field) {
        case FIELD_assignedDroneIds: pp->setAssignedDroneIdsArraySize(size); break;
        default: throw omnetpp::cRuntimeError("Cannot set array size of field %d of class 'TaskAssignment'", field);
    }
}

const char *TaskAssignmentDescriptor::getFieldDynamicTypeString(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldDynamicTypeString(object,field,i);
        field -= base->getFieldCount();
    }
    TaskAssignment *pp = omnetpp::fromAnyPtr<TaskAssignment>(object); (void)pp;
    switch (field) {
        default: return nullptr;
    }
}

std::string TaskAssignmentDescriptor::getFieldValueAsString(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldValueAsString(object,field,i);
        field -= base->getFieldCount();
    }
    TaskAssignment *pp = omnetpp::fromAnyPtr<TaskAssignment>(object); (void)pp;
    switch (field) {
        case FIELD_taskId: return long2string(pp->getTaskId());
        case FIELD_assignedDroneIds: return long2string(pp->getAssignedDroneIds(i));
        default: return "";
    }
}

void TaskAssignmentDescriptor::setFieldValueAsString(omnetpp::any_ptr object, int field, int i, const char *value) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldValueAsString(object, field, i, value);
            return;
        }
        field -= base->getFieldCount();
    }
    TaskAssignment *pp = omnetpp::fromAnyPtr<TaskAssignment>(object); (void)pp;
    switch (field) {
        case FIELD_taskId: pp->setTaskId(string2long(value)); break;
        case FIELD_assignedDroneIds: pp->setAssignedDroneIds(i,string2long(value)); break;
        default: throw omnetpp::cRuntimeError("Cannot set field %d of class 'TaskAssignment'", field);
    }
}

omnetpp::cValue TaskAssignmentDescriptor::getFieldValue(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldValue(object,field,i);
        field -= base->getFieldCount();
    }
    TaskAssignment *pp = omnetpp::fromAnyPtr<TaskAssignment>(object); (void)pp;
    switch (field) {
        case FIELD_taskId: return pp->getTaskId();
        case FIELD_assignedDroneIds: return pp->getAssignedDroneIds(i);
        default: throw omnetpp::cRuntimeError("Cannot return field %d of class 'TaskAssignment' as cValue -- field index out of range?", field);
    }
}

void TaskAssignmentDescriptor::setFieldValue(omnetpp::any_ptr object, int field, int i, const omnetpp::cValue& value) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldValue(object, field, i, value);
            return;
        }
        field -= base->getFieldCount();
    }
    TaskAssignment *pp = omnetpp::fromAnyPtr<TaskAssignment>(object); (void)pp;
    switch (field) {
        case FIELD_taskId: pp->setTaskId(omnetpp::checked_int_cast<int>(value.intValue())); break;
        case FIELD_assignedDroneIds: pp->setAssignedDroneIds(i,omnetpp::checked_int_cast<int>(value.intValue())); break;
        default: throw omnetpp::cRuntimeError("Cannot set field %d of class 'TaskAssignment'", field);
    }
}

const char *TaskAssignmentDescriptor::getFieldStructName(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldStructName(field);
        field -= base->getFieldCount();
    }
    switch (field) {
        default: return nullptr;
    };
}

omnetpp::any_ptr TaskAssignmentDescriptor::getFieldStructValuePointer(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldStructValuePointer(object, field, i);
        field -= base->getFieldCount();
    }
    TaskAssignment *pp = omnetpp::fromAnyPtr<TaskAssignment>(object); (void)pp;
    switch (field) {
        default: return omnetpp::any_ptr(nullptr);
    }
}

void TaskAssignmentDescriptor::setFieldStructValuePointer(omnetpp::any_ptr object, int field, int i, omnetpp::any_ptr ptr) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldStructValuePointer(object, field, i, ptr);
            return;
        }
        field -= base->getFieldCount();
    }
    TaskAssignment *pp = omnetpp::fromAnyPtr<TaskAssignment>(object); (void)pp;
    switch (field) {
        default: throw omnetpp::cRuntimeError("Cannot set field %d of class 'TaskAssignment'", field);
    }
}

Register_Class(TaskCompletion)

TaskCompletion::TaskCompletion() : ::inet::FieldsChunk()
{
}

TaskCompletion::TaskCompletion(const TaskCompletion& other) : ::inet::FieldsChunk(other)
{
    copy(other);
}

TaskCompletion::~TaskCompletion()
{
}

TaskCompletion& TaskCompletion::operator=(const TaskCompletion& other)
{
    if (this == &other) return *this;
    ::inet::FieldsChunk::operator=(other);
    copy(other);
    return *this;
}

void TaskCompletion::copy(const TaskCompletion& other)
{
    this->taskId = other.taskId;
    this->droneId = other.droneId;
}

void TaskCompletion::parsimPack(omnetpp::cCommBuffer *b) const
{
    ::inet::FieldsChunk::parsimPack(b);
    doParsimPacking(b,this->taskId);
    doParsimPacking(b,this->droneId);
}

void TaskCompletion::parsimUnpack(omnetpp::cCommBuffer *b)
{
    ::inet::FieldsChunk::parsimUnpack(b);
    doParsimUnpacking(b,this->taskId);
    doParsimUnpacking(b,this->droneId);
}

int TaskCompletion::getTaskId() const
{
    return this->taskId;
}

void TaskCompletion::setTaskId(int taskId)
{
    handleChange();
    this->taskId = taskId;
}

int TaskCompletion::getDroneId() const
{
    return this->droneId;
}

void TaskCompletion::setDroneId(int droneId)
{
    handleChange();
    this->droneId = droneId;
}

class TaskCompletionDescriptor : public omnetpp::cClassDescriptor
{
  private:
    mutable const char **propertyNames;
    enum FieldConstants {
        FIELD_taskId,
        FIELD_droneId,
    };
  public:
    TaskCompletionDescriptor();
    virtual ~TaskCompletionDescriptor();

    virtual bool doesSupport(omnetpp::cObject *obj) const override;
    virtual const char **getPropertyNames() const override;
    virtual const char *getProperty(const char *propertyName) const override;
    virtual int getFieldCount() const override;
    virtual const char *getFieldName(int field) const override;
    virtual int findField(const char *fieldName) const override;
    virtual unsigned int getFieldTypeFlags(int field) const override;
    virtual const char *getFieldTypeString(int field) const override;
    virtual const char **getFieldPropertyNames(int field) const override;
    virtual const char *getFieldProperty(int field, const char *propertyName) const override;
    virtual int getFieldArraySize(omnetpp::any_ptr object, int field) const override;
    virtual void setFieldArraySize(omnetpp::any_ptr object, int field, int size) const override;

    virtual const char *getFieldDynamicTypeString(omnetpp::any_ptr object, int field, int i) const override;
    virtual std::string getFieldValueAsString(omnetpp::any_ptr object, int field, int i) const override;
    virtual void setFieldValueAsString(omnetpp::any_ptr object, int field, int i, const char *value) const override;
    virtual omnetpp::cValue getFieldValue(omnetpp::any_ptr object, int field, int i) const override;
    virtual void setFieldValue(omnetpp::any_ptr object, int field, int i, const omnetpp::cValue& value) const override;

    virtual const char *getFieldStructName(int field) const override;
    virtual omnetpp::any_ptr getFieldStructValuePointer(omnetpp::any_ptr object, int field, int i) const override;
    virtual void setFieldStructValuePointer(omnetpp::any_ptr object, int field, int i, omnetpp::any_ptr ptr) const override;
};

Register_ClassDescriptor(TaskCompletionDescriptor)

TaskCompletionDescriptor::TaskCompletionDescriptor() : omnetpp::cClassDescriptor(omnetpp::opp_typename(typeid(uavswarmta::TaskCompletion)), "inet::FieldsChunk")
{
    propertyNames = nullptr;
}

TaskCompletionDescriptor::~TaskCompletionDescriptor()
{
    delete[] propertyNames;
}

bool TaskCompletionDescriptor::doesSupport(omnetpp::cObject *obj) const
{
    return dynamic_cast<TaskCompletion *>(obj)!=nullptr;
}

const char **TaskCompletionDescriptor::getPropertyNames() const
{
    if (!propertyNames) {
        static const char *names[] = {  nullptr };
        omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
        const char **baseNames = base ? base->getPropertyNames() : nullptr;
        propertyNames = mergeLists(baseNames, names);
    }
    return propertyNames;
}

const char *TaskCompletionDescriptor::getProperty(const char *propertyName) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    return base ? base->getProperty(propertyName) : nullptr;
}

int TaskCompletionDescriptor::getFieldCount() const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    return base ? 2+base->getFieldCount() : 2;
}

unsigned int TaskCompletionDescriptor::getFieldTypeFlags(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldTypeFlags(field);
        field -= base->getFieldCount();
    }
    static unsigned int fieldTypeFlags[] = {
        FD_ISEDITABLE,    // FIELD_taskId
        FD_ISEDITABLE,    // FIELD_droneId
    };
    return (field >= 0 && field < 2) ? fieldTypeFlags[field] : 0;
}

const char *TaskCompletionDescriptor::getFieldName(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldName(field);
        field -= base->getFieldCount();
    }
    static const char *fieldNames[] = {
        "taskId",
        "droneId",
    };
    return (field >= 0 && field < 2) ? fieldNames[field] : nullptr;
}

int TaskCompletionDescriptor::findField(const char *fieldName) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    int baseIndex = base ? base->getFieldCount() : 0;
    if (strcmp(fieldName, "taskId") == 0) return baseIndex + 0;
    if (strcmp(fieldName, "droneId") == 0) return baseIndex + 1;
    return base ? base->findField(fieldName) : -1;
}

const char *TaskCompletionDescriptor::getFieldTypeString(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldTypeString(field);
        field -= base->getFieldCount();
    }
    static const char *fieldTypeStrings[] = {
        "int",    // FIELD_taskId
        "int",    // FIELD_droneId
    };
    return (field >= 0 && field < 2) ? fieldTypeStrings[field] : nullptr;
}

const char **TaskCompletionDescriptor::getFieldPropertyNames(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldPropertyNames(field);
        field -= base->getFieldCount();
    }
    switch (field) {
        default: return nullptr;
    }
}

const char *TaskCompletionDescriptor::getFieldProperty(int field, const char *propertyName) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldProperty(field, propertyName);
        field -= base->getFieldCount();
    }
    switch (field) {
        default: return nullptr;
    }
}

int TaskCompletionDescriptor::getFieldArraySize(omnetpp::any_ptr object, int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldArraySize(object, field);
        field -= base->getFieldCount();
    }
    TaskCompletion *pp = omnetpp::fromAnyPtr<TaskCompletion>(object); (void)pp;
    switch (field) {
        default: return 0;
    }
}

void TaskCompletionDescriptor::setFieldArraySize(omnetpp::any_ptr object, int field, int size) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldArraySize(object, field, size);
            return;
        }
        field -= base->getFieldCount();
    }
    TaskCompletion *pp = omnetpp::fromAnyPtr<TaskCompletion>(object); (void)pp;
    switch (field) {
        default: throw omnetpp::cRuntimeError("Cannot set array size of field %d of class 'TaskCompletion'", field);
    }
}

const char *TaskCompletionDescriptor::getFieldDynamicTypeString(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldDynamicTypeString(object,field,i);
        field -= base->getFieldCount();
    }
    TaskCompletion *pp = omnetpp::fromAnyPtr<TaskCompletion>(object); (void)pp;
    switch (field) {
        default: return nullptr;
    }
}

std::string TaskCompletionDescriptor::getFieldValueAsString(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldValueAsString(object,field,i);
        field -= base->getFieldCount();
    }
    TaskCompletion *pp = omnetpp::fromAnyPtr<TaskCompletion>(object); (void)pp;
    switch (field) {
        case FIELD_taskId: return long2string(pp->getTaskId());
        case FIELD_droneId: return long2string(pp->getDroneId());
        default: return "";
    }
}

void TaskCompletionDescriptor::setFieldValueAsString(omnetpp::any_ptr object, int field, int i, const char *value) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldValueAsString(object, field, i, value);
            return;
        }
        field -= base->getFieldCount();
    }
    TaskCompletion *pp = omnetpp::fromAnyPtr<TaskCompletion>(object); (void)pp;
    switch (field) {
        case FIELD_taskId: pp->setTaskId(string2long(value)); break;
        case FIELD_droneId: pp->setDroneId(string2long(value)); break;
        default: throw omnetpp::cRuntimeError("Cannot set field %d of class 'TaskCompletion'", field);
    }
}

omnetpp::cValue TaskCompletionDescriptor::getFieldValue(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldValue(object,field,i);
        field -= base->getFieldCount();
    }
    TaskCompletion *pp = omnetpp::fromAnyPtr<TaskCompletion>(object); (void)pp;
    switch (field) {
        case FIELD_taskId: return pp->getTaskId();
        case FIELD_droneId: return pp->getDroneId();
        default: throw omnetpp::cRuntimeError("Cannot return field %d of class 'TaskCompletion' as cValue -- field index out of range?", field);
    }
}

void TaskCompletionDescriptor::setFieldValue(omnetpp::any_ptr object, int field, int i, const omnetpp::cValue& value) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldValue(object, field, i, value);
            return;
        }
        field -= base->getFieldCount();
    }
    TaskCompletion *pp = omnetpp::fromAnyPtr<TaskCompletion>(object); (void)pp;
    switch (field) {
        case FIELD_taskId: pp->setTaskId(omnetpp::checked_int_cast<int>(value.intValue())); break;
        case FIELD_droneId: pp->setDroneId(omnetpp::checked_int_cast<int>(value.intValue())); break;
        default: throw omnetpp::cRuntimeError("Cannot set field %d of class 'TaskCompletion'", field);
    }
}

const char *TaskCompletionDescriptor::getFieldStructName(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldStructName(field);
        field -= base->getFieldCount();
    }
    switch (field) {
        default: return nullptr;
    };
}

omnetpp::any_ptr TaskCompletionDescriptor::getFieldStructValuePointer(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldStructValuePointer(object, field, i);
        field -= base->getFieldCount();
    }
    TaskCompletion *pp = omnetpp::fromAnyPtr<TaskCompletion>(object); (void)pp;
    switch (field) {
        default: return omnetpp::any_ptr(nullptr);
    }
}

void TaskCompletionDescriptor::setFieldStructValuePointer(omnetpp::any_ptr object, int field, int i, omnetpp::any_ptr ptr) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldStructValuePointer(object, field, i, ptr);
            return;
        }
        field -= base->getFieldCount();
    }
    TaskCompletion *pp = omnetpp::fromAnyPtr<TaskCompletion>(object); (void)pp;
    switch (field) {
        default: throw omnetpp::cRuntimeError("Cannot set field %d of class 'TaskCompletion'", field);
    }
}

Register_Class(TaskDropNotification)

TaskDropNotification::TaskDropNotification() : ::inet::FieldsChunk()
{
}

TaskDropNotification::TaskDropNotification(const TaskDropNotification& other) : ::inet::FieldsChunk(other)
{
    copy(other);
}

TaskDropNotification::~TaskDropNotification()
{
}

TaskDropNotification& TaskDropNotification::operator=(const TaskDropNotification& other)
{
    if (this == &other) return *this;
    ::inet::FieldsChunk::operator=(other);
    copy(other);
    return *this;
}

void TaskDropNotification::copy(const TaskDropNotification& other)
{
    this->taskId = other.taskId;
    this->droppingDroneId = other.droppingDroneId;
    this->remainingDuration = other.remainingDuration;
}

void TaskDropNotification::parsimPack(omnetpp::cCommBuffer *b) const
{
    ::inet::FieldsChunk::parsimPack(b);
    doParsimPacking(b,this->taskId);
    doParsimPacking(b,this->droppingDroneId);
    doParsimPacking(b,this->remainingDuration);
}

void TaskDropNotification::parsimUnpack(omnetpp::cCommBuffer *b)
{
    ::inet::FieldsChunk::parsimUnpack(b);
    doParsimUnpacking(b,this->taskId);
    doParsimUnpacking(b,this->droppingDroneId);
    doParsimUnpacking(b,this->remainingDuration);
}

int TaskDropNotification::getTaskId() const
{
    return this->taskId;
}

void TaskDropNotification::setTaskId(int taskId)
{
    handleChange();
    this->taskId = taskId;
}

int TaskDropNotification::getDroppingDroneId() const
{
    return this->droppingDroneId;
}

void TaskDropNotification::setDroppingDroneId(int droppingDroneId)
{
    handleChange();
    this->droppingDroneId = droppingDroneId;
}

double TaskDropNotification::getRemainingDuration() const
{
    return this->remainingDuration;
}

void TaskDropNotification::setRemainingDuration(double remainingDuration)
{
    handleChange();
    this->remainingDuration = remainingDuration;
}

class TaskDropNotificationDescriptor : public omnetpp::cClassDescriptor
{
  private:
    mutable const char **propertyNames;
    enum FieldConstants {
        FIELD_taskId,
        FIELD_droppingDroneId,
        FIELD_remainingDuration,
    };
  public:
    TaskDropNotificationDescriptor();
    virtual ~TaskDropNotificationDescriptor();

    virtual bool doesSupport(omnetpp::cObject *obj) const override;
    virtual const char **getPropertyNames() const override;
    virtual const char *getProperty(const char *propertyName) const override;
    virtual int getFieldCount() const override;
    virtual const char *getFieldName(int field) const override;
    virtual int findField(const char *fieldName) const override;
    virtual unsigned int getFieldTypeFlags(int field) const override;
    virtual const char *getFieldTypeString(int field) const override;
    virtual const char **getFieldPropertyNames(int field) const override;
    virtual const char *getFieldProperty(int field, const char *propertyName) const override;
    virtual int getFieldArraySize(omnetpp::any_ptr object, int field) const override;
    virtual void setFieldArraySize(omnetpp::any_ptr object, int field, int size) const override;

    virtual const char *getFieldDynamicTypeString(omnetpp::any_ptr object, int field, int i) const override;
    virtual std::string getFieldValueAsString(omnetpp::any_ptr object, int field, int i) const override;
    virtual void setFieldValueAsString(omnetpp::any_ptr object, int field, int i, const char *value) const override;
    virtual omnetpp::cValue getFieldValue(omnetpp::any_ptr object, int field, int i) const override;
    virtual void setFieldValue(omnetpp::any_ptr object, int field, int i, const omnetpp::cValue& value) const override;

    virtual const char *getFieldStructName(int field) const override;
    virtual omnetpp::any_ptr getFieldStructValuePointer(omnetpp::any_ptr object, int field, int i) const override;
    virtual void setFieldStructValuePointer(omnetpp::any_ptr object, int field, int i, omnetpp::any_ptr ptr) const override;
};

Register_ClassDescriptor(TaskDropNotificationDescriptor)

TaskDropNotificationDescriptor::TaskDropNotificationDescriptor() : omnetpp::cClassDescriptor(omnetpp::opp_typename(typeid(uavswarmta::TaskDropNotification)), "inet::FieldsChunk")
{
    propertyNames = nullptr;
}

TaskDropNotificationDescriptor::~TaskDropNotificationDescriptor()
{
    delete[] propertyNames;
}

bool TaskDropNotificationDescriptor::doesSupport(omnetpp::cObject *obj) const
{
    return dynamic_cast<TaskDropNotification *>(obj)!=nullptr;
}

const char **TaskDropNotificationDescriptor::getPropertyNames() const
{
    if (!propertyNames) {
        static const char *names[] = {  nullptr };
        omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
        const char **baseNames = base ? base->getPropertyNames() : nullptr;
        propertyNames = mergeLists(baseNames, names);
    }
    return propertyNames;
}

const char *TaskDropNotificationDescriptor::getProperty(const char *propertyName) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    return base ? base->getProperty(propertyName) : nullptr;
}

int TaskDropNotificationDescriptor::getFieldCount() const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    return base ? 3+base->getFieldCount() : 3;
}

unsigned int TaskDropNotificationDescriptor::getFieldTypeFlags(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldTypeFlags(field);
        field -= base->getFieldCount();
    }
    static unsigned int fieldTypeFlags[] = {
        FD_ISEDITABLE,    // FIELD_taskId
        FD_ISEDITABLE,    // FIELD_droppingDroneId
        FD_ISEDITABLE,    // FIELD_remainingDuration
    };
    return (field >= 0 && field < 3) ? fieldTypeFlags[field] : 0;
}

const char *TaskDropNotificationDescriptor::getFieldName(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldName(field);
        field -= base->getFieldCount();
    }
    static const char *fieldNames[] = {
        "taskId",
        "droppingDroneId",
        "remainingDuration",
    };
    return (field >= 0 && field < 3) ? fieldNames[field] : nullptr;
}

int TaskDropNotificationDescriptor::findField(const char *fieldName) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    int baseIndex = base ? base->getFieldCount() : 0;
    if (strcmp(fieldName, "taskId") == 0) return baseIndex + 0;
    if (strcmp(fieldName, "droppingDroneId") == 0) return baseIndex + 1;
    if (strcmp(fieldName, "remainingDuration") == 0) return baseIndex + 2;
    return base ? base->findField(fieldName) : -1;
}

const char *TaskDropNotificationDescriptor::getFieldTypeString(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldTypeString(field);
        field -= base->getFieldCount();
    }
    static const char *fieldTypeStrings[] = {
        "int",    // FIELD_taskId
        "int",    // FIELD_droppingDroneId
        "double",    // FIELD_remainingDuration
    };
    return (field >= 0 && field < 3) ? fieldTypeStrings[field] : nullptr;
}

const char **TaskDropNotificationDescriptor::getFieldPropertyNames(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldPropertyNames(field);
        field -= base->getFieldCount();
    }
    switch (field) {
        default: return nullptr;
    }
}

const char *TaskDropNotificationDescriptor::getFieldProperty(int field, const char *propertyName) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldProperty(field, propertyName);
        field -= base->getFieldCount();
    }
    switch (field) {
        default: return nullptr;
    }
}

int TaskDropNotificationDescriptor::getFieldArraySize(omnetpp::any_ptr object, int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldArraySize(object, field);
        field -= base->getFieldCount();
    }
    TaskDropNotification *pp = omnetpp::fromAnyPtr<TaskDropNotification>(object); (void)pp;
    switch (field) {
        default: return 0;
    }
}

void TaskDropNotificationDescriptor::setFieldArraySize(omnetpp::any_ptr object, int field, int size) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldArraySize(object, field, size);
            return;
        }
        field -= base->getFieldCount();
    }
    TaskDropNotification *pp = omnetpp::fromAnyPtr<TaskDropNotification>(object); (void)pp;
    switch (field) {
        default: throw omnetpp::cRuntimeError("Cannot set array size of field %d of class 'TaskDropNotification'", field);
    }
}

const char *TaskDropNotificationDescriptor::getFieldDynamicTypeString(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldDynamicTypeString(object,field,i);
        field -= base->getFieldCount();
    }
    TaskDropNotification *pp = omnetpp::fromAnyPtr<TaskDropNotification>(object); (void)pp;
    switch (field) {
        default: return nullptr;
    }
}

std::string TaskDropNotificationDescriptor::getFieldValueAsString(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldValueAsString(object,field,i);
        field -= base->getFieldCount();
    }
    TaskDropNotification *pp = omnetpp::fromAnyPtr<TaskDropNotification>(object); (void)pp;
    switch (field) {
        case FIELD_taskId: return long2string(pp->getTaskId());
        case FIELD_droppingDroneId: return long2string(pp->getDroppingDroneId());
        case FIELD_remainingDuration: return double2string(pp->getRemainingDuration());
        default: return "";
    }
}

void TaskDropNotificationDescriptor::setFieldValueAsString(omnetpp::any_ptr object, int field, int i, const char *value) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldValueAsString(object, field, i, value);
            return;
        }
        field -= base->getFieldCount();
    }
    TaskDropNotification *pp = omnetpp::fromAnyPtr<TaskDropNotification>(object); (void)pp;
    switch (field) {
        case FIELD_taskId: pp->setTaskId(string2long(value)); break;
        case FIELD_droppingDroneId: pp->setDroppingDroneId(string2long(value)); break;
        case FIELD_remainingDuration: pp->setRemainingDuration(string2double(value)); break;
        default: throw omnetpp::cRuntimeError("Cannot set field %d of class 'TaskDropNotification'", field);
    }
}

omnetpp::cValue TaskDropNotificationDescriptor::getFieldValue(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldValue(object,field,i);
        field -= base->getFieldCount();
    }
    TaskDropNotification *pp = omnetpp::fromAnyPtr<TaskDropNotification>(object); (void)pp;
    switch (field) {
        case FIELD_taskId: return pp->getTaskId();
        case FIELD_droppingDroneId: return pp->getDroppingDroneId();
        case FIELD_remainingDuration: return pp->getRemainingDuration();
        default: throw omnetpp::cRuntimeError("Cannot return field %d of class 'TaskDropNotification' as cValue -- field index out of range?", field);
    }
}

void TaskDropNotificationDescriptor::setFieldValue(omnetpp::any_ptr object, int field, int i, const omnetpp::cValue& value) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldValue(object, field, i, value);
            return;
        }
        field -= base->getFieldCount();
    }
    TaskDropNotification *pp = omnetpp::fromAnyPtr<TaskDropNotification>(object); (void)pp;
    switch (field) {
        case FIELD_taskId: pp->setTaskId(omnetpp::checked_int_cast<int>(value.intValue())); break;
        case FIELD_droppingDroneId: pp->setDroppingDroneId(omnetpp::checked_int_cast<int>(value.intValue())); break;
        case FIELD_remainingDuration: pp->setRemainingDuration(value.doubleValue()); break;
        default: throw omnetpp::cRuntimeError("Cannot set field %d of class 'TaskDropNotification'", field);
    }
}

const char *TaskDropNotificationDescriptor::getFieldStructName(int field) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldStructName(field);
        field -= base->getFieldCount();
    }
    switch (field) {
        default: return nullptr;
    };
}

omnetpp::any_ptr TaskDropNotificationDescriptor::getFieldStructValuePointer(omnetpp::any_ptr object, int field, int i) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount())
            return base->getFieldStructValuePointer(object, field, i);
        field -= base->getFieldCount();
    }
    TaskDropNotification *pp = omnetpp::fromAnyPtr<TaskDropNotification>(object); (void)pp;
    switch (field) {
        default: return omnetpp::any_ptr(nullptr);
    }
}

void TaskDropNotificationDescriptor::setFieldStructValuePointer(omnetpp::any_ptr object, int field, int i, omnetpp::any_ptr ptr) const
{
    omnetpp::cClassDescriptor *base = getBaseClassDescriptor();
    if (base) {
        if (field < base->getFieldCount()){
            base->setFieldStructValuePointer(object, field, i, ptr);
            return;
        }
        field -= base->getFieldCount();
    }
    TaskDropNotification *pp = omnetpp::fromAnyPtr<TaskDropNotification>(object); (void)pp;
    switch (field) {
        default: throw omnetpp::cRuntimeError("Cannot set field %d of class 'TaskDropNotification'", field);
    }
}

}  // namespace uavswarmta

namespace omnetpp {

}  // namespace omnetpp
