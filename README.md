# znc-timer
A quick and dirty timer for znc, made for fun.
Uses c++14 features, so adopt your znc-buildmod script 
and make sure that your compiler supports it.

How to use:
1) copy the built file to the corresponding modules directory of your 
   znc installation.
2) load it via /msg *status loadmod alarm.
3) use /msg *alarm help to get a list of commands.

Time is given in the format: xxd xxm xxs in any order. 
