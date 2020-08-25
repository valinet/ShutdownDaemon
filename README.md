# Shutdown Daemon
Ever wanted your application to do something specific when the computer is shutting down? For example, I have a Raspberry Pi that automates my office and I would want it to turn off the computer monitor whenever I leave my desk, that including shutting down the computer. At the moment, I use Windows 10 on my PC, so, how do you do that?

The obvious answer is to create a Windows application that spawns a hidden window and listens for [`WM_ENDSESSION`](https://docs.microsoft.com/en-us/windows/win32/shutdown/wm-endsession) and does whatever it needs to do when a shutdown is detected (*lParam* is 0). 

So, what's this all about? Well, the problem arises when you want to differentiate between a shutdown and a restart. The aforementioned window message does not tell you this information. In fact, the docs go as far as telling you it is not possible to determine which of the two is occurring, so they explicitly acknowledge how crippled this message is.

#### Concepts shown in this example (all in C++/Win32):

* Creating a Windows service
* Exploring UIs of other applications using UI Automation
* Using named pipes as a basic IPC mechanism
* Making web requests (e.g. POST) using WinINet

## Why?

Why do I want to differentiate between the two? Because when I restart the computer, it means that I am still sitting in front of it and using it, so there is no point in turning off the monitor. Maybe I am restarting so that I can go in the firmware and change a setting, boot another OS, or simply complete an update install, or whatever. Point is, when restarting, even though most of the times it would not matter the monitor switches off and then on again, there are some cases when it could become very annoying if it did so. So that's how my quest begin to attempt determine this information.

## Attempts

First idea that came to mind was to check the event log. Windows usually writes an entry containing the reason the computer is shutting down/restarting, with ID 1074, a message along the lines:

```
The process Explorer.EXE has initiated the power off of computer THINKPAD on behalf of user THINKPAD\Valentin for the following reason: Other (Unplanned)
 Reason Code: 0x0
 Shutdown Type: power off
 Comment:
```

Great, so just read the message when detecting a shutdown in my application, right? Well, the event is not always written when `WM_ENDSESSION` is received. In fact, it never is. Okay then, I thought, let's have my application spawn at shutdown specifically and check for the event. For this, you can set a logon/ logoff or startup/shutdown script using Group Policy. The paths are:

```
Computer Configuration\Windows Settings\Scripts (Startup/Shutdown)\Shutdown

User Configuration\Windows Settings\Scripts (Logon/Logoff)\Logoff
```

I tried with setting a shutdown script, but the issue is this never run when I was shutting down the computer for me because I had fast startup enabled. Plus, I risk having the network down. So, a logoff script then. Problem is, the event still may not be written by the time the script runs. It is not deterministic. It is especially problematic when shutting down (it is probably affected by the behavior of fast startup). Sometimes it is there, sometimes it is not, and waiting does not seem to help (these scripts have a default run time period of 10 minutes allocated to them, but I find pretty scandalous to delay shutdown just because of this stupid design in Windows). When shutting down, because of fast startup, Windows writes event 42 as well:

```
The system is entering sleep.

Sleep Reason: Application API
```

I could check for that, yet it gets written to the log only after we turn the system back on, so again, useless. So, maybe there is no way to detect this? Well, how could that be, there certainly is at least one place where this information is written, as Windows pretty much shuts down or restarts, it clearly knows what we chose. Also, it in fact is present in multiple places: ever noticed how the logoff screen knows that you are "Signing out"/"Shutting down"/"Restarting"? Clearly there has to be a way to figure a reliable method to determine this.

At first, I was thinking of hooking common Win32 calls like `ExitWindowsEx`, `InitiateSystemShutdown` etc, all calls that applications make in order to request powering off/rebooting the machine, and log the information about what they requested. Then, if I eventually get the thing to happen, I'd have a log off script that checks that information and acts accordingly. Having previous experience with this, I had success with this, in that I was able to hook all processes easily, even though it invloves a lot of work. Though, I decided not to pursue this way because it is too invasive and I had troubles injecting special processes (there are low integrity ones, like Chrome processes, for example, or protected process light processes etc). Also, maybe the application initiates a restart/turn off with some other call that I have not hooked, or maybe I just can't hook it, not to mention that my hooking can fail and I crash the application. So yeah, not an actual solution. Also, I need to hook manually, since old methods like AppInitDLLs and AppCertDLLs on one hand, are pretty much disabled under Secure Boot, and on the other, they hook only certain applications, not all applications on the system. I decided this approach is too messy and gave up on it.

Being in need of another approach, I started investigating how shutdown works. Obviously the component responsible for drawing the logoff UI knows what the system will do, because it displays a different text based on what is about to happen. 

It turns out, the logoff screen is hosted by LogonUI.exe, but the logic is not in there (it calls external logic using CoCreateInstance). Both the logoff and Ctrl+Alt+Del screen are hosted by LogonUI.exe.

Also, I knew the logon UI supports assistive technologies (I have accidentally activated Narrator a few times while in there). This is important, because it means that its UI is exposed into some sort of tree that you could explore, using the right API. That's how Narrator is able to narrate the text of the controls, for example. Being a XAML app, we are not talking about classic EnumChildWindows etc. As I was expecting, the UI is exposed via the Automation API, and it is only a matter of querying the UI tree of the window of LogonUI.exe. Working snippets on how to do this are fortunately ready made provided at MSDN [here](https://docs.microsoft.com/en-us/windows/win32/winauto/uiauto-howto-find-ui-elements) and [here](https://docs.microsoft.com/en-us/windows/win32/winauto/uiauto-howto-walk-uiautomation-tree).

## Solution

So, I decided to try to capture the text that the logoff UI displays when shutting down/restarting, and based on that do the action I need. This way, I don't need to hook any internals, this being pretty future proof and achievable without tons of IDA and research for such a stupid thing (Windows should offer an API for knowing if the computer restarts, just saying...). A problem with this approach is that the text is localized (i.e. displayed in a different language depending on the system settings), and my example here works only if Windows is set to English, but I do not think this is a big issue considering my use case, and also, a workaround could be done around this, it is not that big of a deal. I'd be more concerned about the possibility of them changing the architecture of the logoff screen, than this. 

To be able to find the logoff window, you have to run on the same desktop as it runs on. Turns out, the logoff UI runs in its own desktop, called *WinSta0\Winlogon*. To be able to open a window there, you have to spawn a process having the same security context as Winlogon.exe. For that, you need to be able to duplicate its security context, a thing that will only succeed in an application running on the SYSTEM account. Fortunately, someone wrote a workable piece of code for doing this, sparing me the wasted hours of researching this by myself, available [here](https://stackoverflow.com/questions/3070152/running-a-process-at-the-windows-7-welcome-screen) (there are C# and C++ examples, I wish I found the C++ one before translating the C# code by hand as well). 

Running under system automatically can be achieved in two ways:

* Running a scheduled task at system startup as SYSTEM user. The problem with this is that "At computer startup" in Task Scheduler means when the computer cold boots, not when waking up from fast startup. Once the computer does a hybrid shut down, at next boot, it won't fire the task we created, even though we have it run "At computer startup"; this means another solution is needed:
* Running as a full blown service. Services run as SYSTEM. They are started properly on cold boots, and are suspended on hybrid shut downs, so it will still run when the computer boots again next time. Also, they are not killed until logging off, so there will be plenty of time the service could observe the text on the logoff screen (also, it is worth mentioning, capturing the text should be done before the user is logged off, so that the text is captured correctly considering the possibility that updates may install, which change the text into showing the progress of the installation). Speaking about writing a service, someone gladly wrote a really good example that spares you all the time of finding out how services work and includes a working minimal skeleton, available [here](https://www.codeproject.com/Articles/499465/Simple-Windows-Service-in-Cplusplus).

Now, I could spawn a process on *WinSta0\Winlogon* and busy wait (with a sensible sleep) until the logoff UI is shown. That would not be too efficient though, and would waste resources, even if not that many considering a bigger sleep interval. The problem is also that you may miss the text, if Windows turns off so fast that it caught the daemon during the sleep time. Also, I don't like using a spinlock for this. I wanted to be notified. But how, without deliving into the internals of LogonUI.exe? Fortunately, I can combine this with previous findings: what if I have a script run at logoff that will 'notify' the process running on *WinSta0\Winlogon*?

Okay, so with all that set, here is how I pictured things:

* Create a service that launches a process on *WinSta0\Winlogon*. If the process gets killed, it spawns it again (required, as during logoff, processes on *WinSta0\Winlogon* are killed for some reason)
* Create a process that once launched, waits to be signaled. Once signaled, it extracts the logoff UI status and reports it back to the signaling process. Or it could do the action itself, that depends on how you architecture it. Lastly, it just exists, so that it can be respawned freshly by the service.
* Create a signaling process, that is spawned as a logoff script. In my implementation, I send the command to my Raspberry PI as a POST web request from this process as well, but one could do it, as I said, from the process above.

Last problem is, how can a process signal the other. Using an IPC mechanism, duh. But what IPC method, specifically? Naturally, I thought of window messages, yet this does not work across desktops, as HWNDs are unique to particular sessions or desktops, as far as I know. Also, the wait would be polluted by other messages as well, involving some unnecessary processing in the form of calls to DefWindowProc. So, skip this.

How I decided to do it was using a named pipe. Again, fortunately, I was able to find a quick sample that did the basic work that I required, without having to manually read on MSDN, avialable [here](https://github.com/peter-bloomfield/win32-named-pipes-example/tree/master/src). 

The process running on the *WinSta0\Winlogon* desktop creates a named pipe and calls [ConnectNamedPipe](https://docs.microsoft.com/en-us/windows/win32/api/namedpipeapi/nf-namedpipeapi-connectnamedpipe) on it, this meaning that the application will block until someone connects to the other end of the pipe. When the signaling process is started as a logoff script (at that very moment in time, the logoff screen is just conveniently sitting on the screen), it opens the pipe. This resumes the *WinSta0\Winlogon* which collects the text from LogonUI.exe's window and writes it on the pipe and exits. The signaling process receives this, interprets it, launches the action if it determines the system is shutting down, and exits as well. When the process on *WinSta0\Winlogon* exits, the service will spawn another instance of it, so that the cycle could resume indefinitely.

## Final thoughts

Learning all this stuff definitely came useful to broadening my knowledge. Also, what I find out while doing all of this, is how similar some under the hood things really are when comparing Windows and Linux. Most people think these are so different, yet what I found is that a lot of the stuff is very similar, at least in the way they designed it, if not regarding the actual implementation as well. Pipes are well known on Linux, yet on Windows, they are very capable as well, but not so well known. I mean, yeah sure, you can't beat the *everything's a file* philosophy, but Windows is not that bad either.

Also, I thought this process has to be way easier on Linux than on Windows. Surely, systemd can help a lot (units having the ability to be chained in a logical sense, some starting after others, without everything starting randomly on their own, is a very cool thing), but as far as I looked into, it does not have an easy way to tell requesting processes whether it is doing a power off or a restart either. A starting point may be [here](https://unix.stackexchange.com/questions/401240/how-can-a-systemd-service-detect-that-system-is-going-to-power-off).

Of note is also that this does not work in Windows 7, because Windows 7 displays the same text whether rebooting or shutting down. Not to talk about the fact that the logoff screen in Windows 7 is entirely different under the hood from the one in Windows 10 (I don't think it is done in XAML, but maybe it is actually easier to hook, being classic Win32 UI).

As well, I think this example is really powerful because it works with no issues on computers having fast startup enabled, which nowadays represent the majority of computers running Windows 10. Older workarounds, as described above, were mainly designed in the Windows 7 days and tend to brake completely when this different shut down behavior is present.

Lastly, I wish Microsoft would just provide a simple C-like Win32 API call for this, it does not have any security implications and may serve a few folks who have a use case similar to mine. Until then, this is what it is.

## How to run?

Clone this repo and compile the solution. I used Visual Studio 2019 (v142), and the latest 10.0 SDK available on my system. This should not really matter, it should work on any recent SDK.

You'll get 3 executables; keep them in the same folder:

* ShutdownDaemonLauncher.exe - this is the service. Fire up an elevated command window and register it on the system with a command like `sc create "ShutdownDaemonService" binPath=C:\...\ShutdownDaemonLauncher.exe`
* ShutdownDaemon.exe - this is the process that gets spawned by the service on *WinSta0\Winlogon*.
* ShutdownDaemonNotifier.exe - this has to be registered as a logoff script using Group Policy at `User Configuration\Windows Settings\Scripts (Logon/Logoff)\Logoff`. If you have a Home version of Windows, which Microsoft cripples by not including Group Policy, you can register the script manually via the Registry, as described [here](https://social.technet.microsoft.com/Forums/windows/en-US/5c0d7e87-6dec-4f87-8ef2-f43b4064d4d5/execution-of-a-script-at-every-logoff-of-any-user?forum=w7itprosecurity) and [here](https://social.technet.microsoft.com/Forums/windows/en-US/5c0d7e87-6dec-4f87-8ef2-f43b4064d4d5/execution-of-a-script-at-every-logoff-of-any-user?forum=w7itprosecurity)

Good luck!