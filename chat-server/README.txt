A simple chat server using libev and winsock2.
-------------------------------------------------------------------------------

Based on the libevent example from https://github.com/jasonish/libevent-examples.

Libev 4.15 is embedded in the project.

USAGE

    Run the server:

        ./chat-server

    Then telnet to localhost port 5555 from multiple terminals.
    Anything you type in one terminal will be sent to the other
    connected terminals.
