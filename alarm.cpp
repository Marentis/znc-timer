/*
* A simple alarm/timer module for ZNC.
* Version 0.4.1-beta
* Copyright (c) 2016, Alexander Schwarz
* Contact: alexander1.schwarz@st.oth-regensburg.de
* License: FreeBSD License
*/

#include <iostream>
#include <string>
#include <regex>
#include <mutex>
#include <thread>
#include <chrono>
#include <condition_variable>
#include <iterator>
#include <list>
#include "znc/IRCNetwork.h"
#include "znc/Modules.h"

using namespace std;

namespace parser
{
auto secs_from_string ( const CString &sLine ) -> long long {
    smatch match;
    auto seconds = 0LL;

    if ( regex_search ( sLine, match, regex("([0-9]{1,2})s")) )
        seconds += stoll ( match[ 1 ] );
    if ( regex_search ( sLine, match, regex("([0-9]{1,2})m") ) )
        seconds += ( stoll ( match[ 1 ] ) * 60 );
    if ( regex_search ( sLine, match, regex("([0-9]{1,2})h") ) )
        seconds += ( stoll ( match[ 1 ] ) * 3600 );
    if ( regex_search ( sLine, match, regex("([0-9]{1,3})d") ) )
        seconds += ( stoll ( match[ 1 ] ) * 86400 );

    return seconds;
}
auto string_from_secs ( const long long endTime ) -> string {
    auto rest = endTime - time(0);
    auto hours = rest / 3600;
    rest -= hours * 3600;
    auto minutes = rest / 60;
    rest -= minutes * 60;
    auto seconds = rest;
    std::string minutesString, secondsString;
    if (minutes < 10)
    {
        minutesString = "0" + to_string(minutes);
    } else {
        minutesString = to_string(minutes);
    }
    if (seconds < 10)
    {
        secondsString = "0" + to_string(seconds);
    } else {
        secondsString = to_string(seconds);
    }
    auto temp = string{to_string(hours) + ":" + minutesString + ":" + secondsString};
    return temp;
}
} // end of namespace parser

class Timer
{
public:
    Timer ( const CString &sLine, unsigned int id ) 
    {
        start_time_ = time ( 0 );
        end_time_ = parser::secs_from_string(sLine) + start_time_;
        const unsigned int reason_length = sLine.size() > REASON_LENGTH_MAX ? REASON_LENGTH_MAX : sLine.size() -1; // make sure to only use up to 512 characters for the reason which should be plenty
        reason_ = sLine.substr ( 4, reason_length  );
        this->timer_id_ = id;
    }
    auto get_start_time  ( ) const
    {
        return start_time_;
    }
    auto get_end_time ( ) const
    {
        return end_time_;
    }
    auto timer_ran_out (  ) const -> bool
    {
        return time ( 0 ) >= end_time_;
    }
    auto get_timer ( ) const
    {
        return reason_;
    }
    auto get_id ( ) const
    {
        return timer_id_;
    }
    auto get_remaining_time ( ) const -> string
    {
        return parser::string_from_secs(end_time_);
    }
private:
	const unsigned int REASON_LENGTH_MAX{512};
    long long int start_time_ = 0LL;
    long long int end_time_ = 0LL;
    unsigned int timer_id_ = 0u;
    string reason_ = "Default"s;
};

class CAlarm : public CModule
{
public:
    MODCONSTRUCTOR( CAlarm )
    {
        AddHelpCommand ( );
        AddCommand ( "add", static_cast<CModCommand::ModCmdFunc>(&CAlarm::add_timer), "reason",
                     "Add a timer with <reason>" );
        AddCommand ( "remove", static_cast<CModCommand::ModCmdFunc>(&CAlarm::remove_timer), "timer id",
                     "Remove a timer" );
        AddCommand ( "list", static_cast<CModCommand::ModCmdFunc>(&CAlarm::list_timers), " ",
                     "List all timers" );
        t1_ = thread ( [ this ] ( ) {
            loopFunc ( );
        } );
    }
    virtual ~CAlarm ( ) override
    {
        check_loop_ = false;
        cv_.notify_all();
        t1_.join ( );
    }
    virtual bool OnLoad ( const CString &sArgs, CString &sMessage ) override
    {
        return true;
    }
    void add_timer ( const CString &sLine )
    {
        lock_guard<mutex> lock ( mutex_ );
        if ( timer_list_.size ( ) >= timer_limit_ ) {
            PutModule ( "Too many timers running, can't create a new one." );
            return;
        }
        timer_list_.emplace_back ( sLine, ++timer_id_ );
        sort_timers();
        PutModule("Timer added.");
        cv_.notify_all();
    }
    void sort_timers ()
    {
        timer_list_.sort([]( Timer a, Timer b) {
            return a.get_end_time() < b.get_end_time();
        });
    }
    void remove_timer ( const CString &sLine )
    {
        smatch match_id;
        unsigned int id { timer_limit_ };
        regex idReg { "([0-9]{1,5})" };
        if ( regex_search ( sLine, match_id, idReg ) ) id = stoi ( match_id[ 1 ] );
        lock_guard<mutex> lock ( mutex_ );
        for ( auto vecIt = timer_list_.begin(); vecIt != timer_list_.end(); vecIt++ ) {
            if ( id == vecIt->get_id() ) {
                timer_list_.erase(vecIt);
                PutModule("Removed the timer.\n");
                return;
            }
        }
        PutModule ( "Timer doesn't exist.\n" );
    }
    void list_timers ( const CString &sLine )
    {
        lock_guard<mutex> lock ( mutex_ );
        if ( timer_list_.empty ( ) ) PutModule ( "There are no timers running at the moment.\n" );
        for ( auto &t : timer_list_ ) {
            PutModule ( "Timer: " + t.get_timer ( ) +
                        ". Timer id: " + to_string( t.get_id ( )) );
            PutModule ( "Expires in: " + t.get_remaining_time ( ) );
        }
    }
    void loopFunc ()
    {
        while ( check_loop_ ) {
            std::unique_lock<std::mutex> lock(mutex_);
            if ( timer_list_.empty() ) cv_.wait(lock);
            else {
                cv_.wait_for(lock, std::chrono::seconds( timer_list_.front().get_end_time()-std::time(0) ) );
                if ( timer_list_.front().timer_ran_out (  ) ) {
                    PutModule ( "Timer expired: " + timer_list_.front().get_timer ( ) );
                    timer_list_.pop_front();
                }
            }
        }
    }

private:
    const unsigned int timer_limit_ = 16u;
    unsigned int timer_id_ = 0u;
    std::list<Timer> timer_list_;
    std::condition_variable cv_;
    mutex mutex_{};
    bool check_loop_{true};
    thread t1_;
};
MODULEDEFS( CAlarm, "A simple alarm clock" );
