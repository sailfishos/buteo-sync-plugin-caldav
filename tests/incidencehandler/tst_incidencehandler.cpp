#include <QtTest/QtTest>

#include <event.h>
#include <todo.h>
#include <journal.h>
#include <alarm.h>
#include <attachment.h>
#include <incidencehandler.h>

using namespace KCalCore;

class tst_IncidenceHandler : public QObject
{
    Q_OBJECT

private slots:
    // Common tests (incidence)
    void changedEventDurationMakesDifferent();
    void changedEventDurationPresenceMakesDifferent();
    void changedEventReadOnlyStateMakesDifferent();
    void changedEventDescriptionMakesDifferent();
    void changedEventAltDescriptionMakesDifferent();
    void changedEventCommentsMakesDifferent();
    void changedEventContactsMakesDifferent();
    void changedEventCategoriesMakesDifferent();
    void changedEventStatusMakesDifferent();
    void changedEventCustomStatusMakesDifferent();
    void changedEventSummaryMakesDifferent();
    void changedEventSecrecyMakesDifferent();
    void changedEventGeoPresenceMakesDifferent();
    void changedEventLocationMakesDifferent();
    void changedEventLatitudeMakesDifferent();
    void changedEventLongitudeMakesDifferent();
    void changedEventRecurrenceMakesDifferent();
    void changedEventStartTimeMakesDifferent();
    void changedAllDayEventStartDateMakesDifferent();
    void changedEventAlarmsMakesDifferent();
    void changedEventAttachmentsMakesDifferent();

    // Event specific tests
    void changedEventEndTimeMakesDifferent();
    void changedAllDayEventEndDateMakesDifferent();
    void changedEventTransparencyMakesDifferent();

    // Todo specific tests
    void changedTodoCompletionStatusMakesDifferent();
    void changedTodoCompletionStatusByDateMakesDifferent();
    void changedTodoCompletionDateMakesDifferent();
    void changedTodoDueDateMakesDifferent();
    void changedTodoRecurrenceDueDateMakesDifferent();
    void changedTodoPercentCompletedMakesDifferent();
};

void tst_IncidenceHandler::changedEventDurationMakesDifferent()
{
    Incidence::Ptr event1 = Incidence::Ptr(new Event);
    event1->setDuration(Duration(3600));
    Incidence::Ptr event2 = Incidence::Ptr(new Event);
    IncidenceHandler::copyIncidenceProperties(event2, event1);
    QVERIFY(IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
    event2->setDuration(1800);
    QVERIFY(!IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
}

void tst_IncidenceHandler::changedEventDurationPresenceMakesDifferent()
{
    Incidence::Ptr event1 = Incidence::Ptr(new Event);
    event1->setHasDuration(false);
    Incidence::Ptr event2 = Incidence::Ptr(new Event);
    IncidenceHandler::copyIncidenceProperties(event2, event1);
    QVERIFY(IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
    event2->setHasDuration(true);
    QVERIFY(!IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
}

void tst_IncidenceHandler::changedEventReadOnlyStateMakesDifferent()
{
    Incidence::Ptr event1 = Incidence::Ptr(new Event);
    event1->setReadOnly(false);
    Incidence::Ptr event2 = Incidence::Ptr(new Event);
    IncidenceHandler::copyIncidenceProperties(event2, event1);
    QVERIFY(IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
    event2->setReadOnly(true);
    QVERIFY(!IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
}

void tst_IncidenceHandler::changedEventDescriptionMakesDifferent()
{
    Incidence::Ptr event1 = Incidence::Ptr(new Event);
    event1->setDescription("A description");
    Incidence::Ptr event2 = Incidence::Ptr(new Event);
    IncidenceHandler::copyIncidenceProperties(event2, event1);
    QVERIFY(IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
    event2->setDescription("Another description");
    QVERIFY(!IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
}

void tst_IncidenceHandler::changedEventAltDescriptionMakesDifferent()
{
    Incidence::Ptr event1 = Incidence::Ptr(new Event);
    event1->setAltDescription("A description");
    Incidence::Ptr event2 = Incidence::Ptr(new Event);
    IncidenceHandler::copyIncidenceProperties(event2, event1);
    QVERIFY(IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
    event2->setAltDescription("Another description");
    QVERIFY(!IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
}

void tst_IncidenceHandler::changedEventCommentsMakesDifferent()
{
    Incidence::Ptr event1 = Incidence::Ptr(new Event);
    event1->addComment("A comment");
    Incidence::Ptr event2 = Incidence::Ptr(new Event);
    IncidenceHandler::copyIncidenceProperties(event2, event1);
    QVERIFY(IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
    event2->addComment("A second comment");
    QVERIFY(!IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
}

void tst_IncidenceHandler::changedEventContactsMakesDifferent()
{
    Incidence::Ptr event1 = Incidence::Ptr(new Event);
    event1->addContact("Alice");
    Incidence::Ptr event2 = Incidence::Ptr(new Event);
    IncidenceHandler::copyIncidenceProperties(event2, event1);
    QVERIFY(IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
    event2->addContact("Bob");
    QVERIFY(!IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
}

void tst_IncidenceHandler::changedEventCategoriesMakesDifferent()
{
    Incidence::Ptr event1 = Incidence::Ptr(new Event);
    QStringList cat1;
    cat1 << "Category A";
    event1->setCategories(cat1);
    Incidence::Ptr event2 = Incidence::Ptr(new Event);
    IncidenceHandler::copyIncidenceProperties(event2, event1);
    QVERIFY(IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
    QStringList cat2;
    cat2 << "Category B";
    event2->setCategories(cat2);
    QVERIFY(!IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
}

void tst_IncidenceHandler::changedEventStatusMakesDifferent()
{
    Incidence::Ptr event1 = Incidence::Ptr(new Event);
    event1->setStatus(Incidence::StatusConfirmed);
    Incidence::Ptr event2 = Incidence::Ptr(new Event);
    IncidenceHandler::copyIncidenceProperties(event2, event1);
    QVERIFY(IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
    event2->setStatus(Incidence::StatusCanceled);
    QVERIFY(!IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
}

void tst_IncidenceHandler::changedEventCustomStatusMakesDifferent()
{
    Incidence::Ptr event1 = Incidence::Ptr(new Event);
    event1->setCustomStatus("A status");
    Incidence::Ptr event2 = Incidence::Ptr(new Event);
    IncidenceHandler::copyIncidenceProperties(event2, event1);
    QVERIFY(IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
    event2->setCustomStatus("Another status");
    QVERIFY(!IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
}

void tst_IncidenceHandler::changedEventSummaryMakesDifferent()
{
    Incidence::Ptr event1 = Incidence::Ptr(new Event);
    event1->setSummary("A summary");
    Incidence::Ptr event2 = Incidence::Ptr(new Event);
    IncidenceHandler::copyIncidenceProperties(event2, event1);
    QVERIFY(IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
    event2->setSummary("Another summary");
    QVERIFY(!IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
}

void tst_IncidenceHandler::changedEventSecrecyMakesDifferent()
{
    Incidence::Ptr event1 = Incidence::Ptr(new Event);
    event1->setSecrecy(Incidence::SecrecyPublic);
    Incidence::Ptr event2 = Incidence::Ptr(new Event);
    IncidenceHandler::copyIncidenceProperties(event2, event1);
    QVERIFY(IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
    event2->setSecrecy(Incidence::SecrecyPrivate);
    QVERIFY(!IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
}

void tst_IncidenceHandler::changedEventGeoPresenceMakesDifferent()
{
    Incidence::Ptr event1 = Incidence::Ptr(new Event);
    event1->setHasGeo(false);
    Incidence::Ptr event2 = Incidence::Ptr(new Event);
    IncidenceHandler::copyIncidenceProperties(event2, event1);
    QVERIFY(IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
    event2->setHasGeo(true);
    QVERIFY(!IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
}

void tst_IncidenceHandler::changedEventLocationMakesDifferent()
{
    Incidence::Ptr event1 = Incidence::Ptr(new Event);
    event1->setLocation("Location 1");
    Incidence::Ptr event2 = Incidence::Ptr(new Event);
    IncidenceHandler::copyIncidenceProperties(event2, event1);
    QVERIFY(IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
    event2->setLocation("Location 2");
    QVERIFY(!IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
}

void tst_IncidenceHandler::changedEventLatitudeMakesDifferent()
{
    Incidence::Ptr event1 = Incidence::Ptr(new Event);
    event1->setGeoLatitude(45.0);
    Incidence::Ptr event2 = Incidence::Ptr(new Event);
    IncidenceHandler::copyIncidenceProperties(event2, event1);
    QVERIFY(IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
    event2->setGeoLatitude(60.0);
    QVERIFY(!IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
}

void tst_IncidenceHandler::changedEventLongitudeMakesDifferent()
{
    Incidence::Ptr event1 = Incidence::Ptr(new Event);
    event1->setGeoLongitude(0.0);
    Incidence::Ptr event2 = Incidence::Ptr(new Event);
    IncidenceHandler::copyIncidenceProperties(event2, event1);
    QVERIFY(IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
    event2->setGeoLongitude(-80.0);
    QVERIFY(!IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
}

void tst_IncidenceHandler::changedEventRecurrenceMakesDifferent()
{
    Incidence::Ptr event1 = Incidence::Ptr(new Event);
    Recurrence* event1Recurrence = event1->recurrence();
    QVERIFY(event1Recurrence != NULL);
    event1Recurrence->setMonthly(1);
    Incidence::Ptr event2 = Incidence::Ptr(new Event);
    IncidenceHandler::copyIncidenceProperties(event2, event1);
    QVERIFY(IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
    Recurrence* event2Recurrence = event2->recurrence();
    QVERIFY(event2Recurrence != NULL);
    event2Recurrence->setWeekly(3);
    QVERIFY(!IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
}

void tst_IncidenceHandler::changedEventStartTimeMakesDifferent()
{
    Incidence::Ptr event1 = Incidence::Ptr(new Event);
    event1->setAllDay(false);
    event1->setDtStart(KDateTime(QDate(2012, 10, 10), QTime(9, 45), KDateTime::Spec(KDateTime::UTC)));
    Incidence::Ptr event2 = Incidence::Ptr(new Event);
    IncidenceHandler::copyIncidenceProperties(event2, event1);
    QVERIFY(IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
    event2->setDtStart(KDateTime(QDate(2012, 10, 10), QTime(16, 0), KDateTime::Spec(KDateTime::UTC)));
    QVERIFY(!IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
}

void tst_IncidenceHandler::changedAllDayEventStartDateMakesDifferent()
{
    Incidence::Ptr event1 = Incidence::Ptr(new Event);
    event1->setAllDay(true);
    event1->setDtStart(KDateTime(QDate(2012, 10, 10), KDateTime::Spec(KDateTime::UTC)));
    Incidence::Ptr event2 = Incidence::Ptr(new Event);
    IncidenceHandler::copyIncidenceProperties(event2, event1);
    QVERIFY(IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
    event2->setDtStart(KDateTime(QDate(2013, 1, 1), KDateTime::Spec(KDateTime::UTC)));
    QVERIFY(!IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
}

void tst_IncidenceHandler::changedEventEndTimeMakesDifferent()
{
    Event::Ptr event1 = Event::Ptr(new Event);
    event1->setAllDay(false);
    event1->setDtEnd(KDateTime(QDate(2012, 10, 10), QTime(9, 45), KDateTime::Spec(KDateTime::UTC)));
    Event::Ptr event2 = Event::Ptr(new Event);
    IncidenceHandler::copyIncidenceProperties(event2, event1);
    QVERIFY(IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
    event2->setDtEnd(KDateTime(QDate(2012, 10, 10), QTime(16, 0), KDateTime::Spec(KDateTime::UTC)));
    QVERIFY(!IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
}

void tst_IncidenceHandler::changedAllDayEventEndDateMakesDifferent()
{
    Event::Ptr event1 = Event::Ptr(new Event);
    event1->setAllDay(true);
    event1->setDtEnd(KDateTime(QDate(2012, 10, 10), KDateTime::Spec(KDateTime::UTC)));
    Event::Ptr event2 = Event::Ptr(new Event);
    IncidenceHandler::copyIncidenceProperties(event2, event1);
    QVERIFY(IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
    event2->setDtEnd(KDateTime(QDate(2013, 1, 1), KDateTime::Spec(KDateTime::UTC)));
    QVERIFY(!IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
}

void tst_IncidenceHandler::changedEventTransparencyMakesDifferent()
{
    Event::Ptr event1 = Event::Ptr(new Event);
    event1->setTransparency(Event::Opaque);
    Event::Ptr event2 = Event::Ptr(new Event);
    IncidenceHandler::copyIncidenceProperties(event2, event1);
    QVERIFY(IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
    event2->setTransparency(Event::Transparent);
    QVERIFY(!IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
}

void tst_IncidenceHandler::changedEventAlarmsMakesDifferent()
{
    Event::Ptr event1 = Event::Ptr(new Event);
    Alarm::Ptr alarm1 = event1->newAlarm();
    alarm1->setDisplayAlarm("Alarm 1");
    Event::Ptr event2 = Event::Ptr(new Event);
    IncidenceHandler::copyIncidenceProperties(event2, event1);
    QVERIFY(IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
    event2->clearAlarms();
    Alarm::Ptr alarm2 = event2->newAlarm();
    alarm2->setDisplayAlarm("Alarm 2");
    QVERIFY(!IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
}

void tst_IncidenceHandler::changedEventAttachmentsMakesDifferent()
{
    Event::Ptr event1 = Event::Ptr(new Event);
    QString uri1("http://example.com/resource/one/");
    Attachment::Ptr attachment1 = Attachment::Ptr(new Attachment(uri1));
    event1->addAttachment(attachment1);
    Event::Ptr event2 = Event::Ptr(new Event);
    IncidenceHandler::copyIncidenceProperties(event2, event1);
    QVERIFY(IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
    event2->clearAttachments();
    QString uri2("http://example.com/resource/two/");
    Attachment::Ptr attachment2 = Attachment::Ptr(new Attachment(uri2));
    event2->addAttachment(attachment2);
    QVERIFY(!IncidenceHandler::copiedPropertiesAreEqual(event1, event2));
}

void tst_IncidenceHandler::changedTodoCompletionStatusMakesDifferent()
{
    Todo::Ptr todo1 = Todo::Ptr(new Todo);
    todo1->setCompleted(false);
    Todo::Ptr todo2 = Todo::Ptr(new Todo);
    IncidenceHandler::copyIncidenceProperties(todo2, todo1);
    QVERIFY(IncidenceHandler::copiedPropertiesAreEqual(todo1, todo2));
    todo2->setCompleted(true);
    QVERIFY(!IncidenceHandler::copiedPropertiesAreEqual(todo1, todo2));
}

void tst_IncidenceHandler::changedTodoCompletionStatusByDateMakesDifferent()
{
    Todo::Ptr todo1 = Todo::Ptr(new Todo);
    todo1->setCompleted(false);
    Todo::Ptr todo2 = Todo::Ptr(new Todo);
    IncidenceHandler::copyIncidenceProperties(todo2, todo1);
    QVERIFY(IncidenceHandler::copiedPropertiesAreEqual(todo1, todo2));
    todo2->setCompleted(KDateTime(QDate(2010, 12, 13), QTime(12, 13, 14), KDateTime::Spec(KDateTime::UTC)));
    QVERIFY(!IncidenceHandler::copiedPropertiesAreEqual(todo1, todo2));
}

void tst_IncidenceHandler::changedTodoCompletionDateMakesDifferent()
{
    Todo::Ptr todo1 = Todo::Ptr(new Todo);
    todo1->setCompleted(KDateTime(QDate(2009, 1, 2), QTime(3, 4, 5), KDateTime::Spec(KDateTime::UTC)));
    Todo::Ptr todo2 = Todo::Ptr(new Todo);
    IncidenceHandler::copyIncidenceProperties(todo2, todo1);
    QVERIFY(IncidenceHandler::copiedPropertiesAreEqual(todo1, todo2));
    todo2->setCompleted(KDateTime(QDate(2010, 12, 13), QTime(12, 13, 14), KDateTime::Spec(KDateTime::UTC)));
    QVERIFY(!IncidenceHandler::copiedPropertiesAreEqual(todo1, todo2));
}

void tst_IncidenceHandler::changedTodoRecurrenceDueDateMakesDifferent()
{
    Todo::Ptr todo1 = Todo::Ptr(new Todo);
    todo1->setDtRecurrence(KDateTime(QDate(2009, 1, 2), QTime(3, 4, 5), KDateTime::Spec(KDateTime::UTC)));
    Todo::Ptr todo2 = Todo::Ptr(new Todo);
    IncidenceHandler::copyIncidenceProperties(todo2, todo1);
    QVERIFY(IncidenceHandler::copiedPropertiesAreEqual(todo1, todo2));
    todo2->setDtRecurrence(KDateTime(QDate(2010, 12, 13), QTime(12, 13, 14), KDateTime::Spec(KDateTime::UTC)));
    QVERIFY(!IncidenceHandler::copiedPropertiesAreEqual(todo1, todo2));
}

void tst_IncidenceHandler::changedTodoPercentCompletedMakesDifferent()
{
    Todo::Ptr todo1 = Todo::Ptr(new Todo);
    todo1->setPercentComplete(50);
    Todo::Ptr todo2 = Todo::Ptr(new Todo);
    IncidenceHandler::copyIncidenceProperties(todo2, todo1);
    QVERIFY(IncidenceHandler::copiedPropertiesAreEqual(todo1, todo2));
    todo2->setPercentComplete(75);
    QVERIFY(!IncidenceHandler::copiedPropertiesAreEqual(todo1, todo2));
}

QTEST_MAIN(tst_IncidenceHandler)
#include "tst_incidencehandler.moc"
