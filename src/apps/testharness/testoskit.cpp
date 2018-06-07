// Standard Includes -----------------------------------------------------------
#include <stdio.h>

// System Includes -------------------------------------------------------------
#include <Application.h>
#include <Looper.h>
#include <Message.h>
#include <OS.h>

// Project Includes ------------------------------------------------------------

// Local Includes --------------------------------------------------------------

// Local Defines ---------------------------------------------------------------

// Globals ---------------------------------------------------------------------
sem_id gThreadLock = create_sem(0, "gThreadLock");

class TLooper1 : public BLooper
{
	public:
		TLooper1() : BLooper() {;}
		void MessageReceived(BMessage* msg)
		{
			switch (msg->what)
			{
				case '2345':
					printf("Got message '2345' in %s\n", __PRETTY_FUNCTION__);
					release_sem(gThreadLock);
					break;
				default:
					BLooper::MessageReceived(msg);
			}
		}
};

class TLooper2 : public BLooper
{
	public:
		TLooper2(BMessenger target) : BLooper(), fTarget(target) {;}
		void MessageReceived(BMessage* msg)
		{
			switch (msg->what)
			{
				case '1234':
					printf("Got message '1234' in %s\n", __PRETTY_FUNCTION__);
					fTarget.SendMessage('2345');
					break;
				default:
					BLooper::MessageReceived(msg);
					break;
			}
		}

	private:
		BMessenger fTarget;
};

int main()
{
	BLooper*	fLooper1 = new TLooper1;
	BLooper*	fLooper2 = new TLooper2(fLooper1);
	fLooper1->Run();
	fLooper2->Run();
	printf("Sending message '1234' in %s\n", __PRETTY_FUNCTION__);
	BMessenger(fLooper2).SendMessage('1234');

	printf("Message '1234' sent, waiting for loopers\n");

	// Wait for loopers to finish
	acquire_sem(gThreadLock);

	return 0;
}
