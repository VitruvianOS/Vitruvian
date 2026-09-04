/*
 * Copyright 2026, Vitruvian. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "MainWindow.h"

#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>

#include <Alert.h>
#include <Application.h>
#include <Button.h>
#include <LayoutBuilder.h>
#include <Message.h>
#include <ScrollView.h>
#include <String.h>
#include <TextView.h>


static const uint32 kMsgRun = 'run.';
static const uint32 kMsgResult = 'rslt';

static const char* kHelperPath = "/usr/libexec/privilegedguy-helper";

// BMessage fields carried by kMsgResult, filled in on the worker thread.
static const char* kFieldExitCode = "exit_code";
static const char* kFieldOutput = "output";
static const char* kFieldSpawnFailed = "spawn_failed";


MainWindow::MainWindow()
	:
	BWindow(BRect(100, 100, 560, 420), "PrivilegedGuy", B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS)
{
	fRunButton = new BButton("run", "Run Privileged Command",
		new BMessage(kMsgRun));

	fOutputView = new BTextView("output");
	fOutputView->SetText(
		"Click \"Run Privileged Command\" to invoke pkexec on "
		"privilegedguy-helper. A real auth_admin polkit prompt should "
		"appear via vos-polkit-agent; enter the admin password there.\n");
	fOutputView->MakeEditable(false);
	fOutputView->SetWordWrap(true);

	BScrollView* scroller = new BScrollView("scroller", fOutputView,
		0, false, true);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(fRunButton)
		.Add(scroller);

	CenterOnScreen();
	Show();
}


bool
MainWindow::QuitRequested()
{
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


void
MainWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgRun:
			_RunPrivilegedCommand();
			break;

		case kMsgResult:
		{
			bool spawnFailed = false;
			message->FindBool(kFieldSpawnFailed, &spawnFailed);

			BString text;
			if (spawnFailed) {
				text = "Could not start pkexec.\n";
			} else {
				int32 exitCode = -1;
				BString output;
				message->FindInt32(kFieldExitCode, &exitCode);
				message->FindString(kFieldOutput, &output);

				text.SetToFormat("pkexec exit code: %" B_PRId32 "\n\n",
					exitCode);
				text << "Output:\n" << output;
			}

			fOutputView->SetText(text.String());
			fRunButton->SetEnabled(true);
			break;
		}

		default:
			BWindow::MessageReceived(message);
	}
}


// Runs on a worker thread: fork()+waitpid() blocks as long as the
// polkit-agent password dialog is up.
static int32
run_helper_thread(void* data)
{
	BWindow* window = (BWindow*)data;

	int outPipe[2];
	if (pipe(outPipe) < 0) {
		BMessage result(kMsgResult);
		result.AddBool(kFieldSpawnFailed, true);
		if (window->Lock()) {
			window->PostMessage(&result);
			window->Unlock();
		}
		return 0;
	}

	pid_t pid = fork();
	if (pid < 0) {
		close(outPipe[0]);
		close(outPipe[1]);
		BMessage result(kMsgResult);
		result.AddBool(kFieldSpawnFailed, true);
		if (window->Lock()) {
			window->PostMessage(&result);
			window->Unlock();
		}
		return 0;
	}

	if (pid == 0) {
		dup2(outPipe[1], STDOUT_FILENO);
		dup2(outPipe[1], STDERR_FILENO);
		close(outPipe[0]);
		close(outPipe[1]);
		execl("/usr/bin/pkexec", "pkexec", kHelperPath, (char*)NULL);
		_exit(127);
	}
	close(outPipe[1]);

	BString output;
	const size_t kMaxOutput = 8192;
	char readBuf[4096];
	for (;;) {
		ssize_t n = read(outPipe[0], readBuf, sizeof(readBuf));
		if (n < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (n == 0)
			break;
		output.Append(readBuf, n);
		if (output.Length() > (int32)kMaxOutput)
			output.Remove(0, output.Length() - kMaxOutput);
	}
	close(outPipe[0]);

	int status = 0;
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
		;

	int32 exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

	BMessage result(kMsgResult);
	result.AddInt32(kFieldExitCode, exitCode);
	result.AddString(kFieldOutput, output);
	if (window->Lock()) {
		window->PostMessage(&result);
		window->Unlock();
	}
	return 0;
}


void
MainWindow::_RunPrivilegedCommand()
{
	fRunButton->SetEnabled(false);
	fOutputView->SetText("Running pkexec... a password prompt should "
		"appear shortly.\n");

	thread_id worker = spawn_thread(run_helper_thread, "privilegedguy-run",
		B_NORMAL_PRIORITY, this);
	if (worker < 0 || resume_thread(worker) != B_OK) {
		fRunButton->SetEnabled(true);
		BAlert* alert = new BAlert("PrivilegedGuy",
			"Could not start the worker thread.", "OK");
		alert->Go();
	}
}
