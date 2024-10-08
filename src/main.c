#include "definitions.h"
#include <stdio.h>
#include "socket.h"
#include "address.h"
#include "network_io.h"
#include "fdwatch.h"
#include "connection.h"

int cleanup_and_exit(SOCKET server_socket, LPFDWATCH *main_fdw, int exit_code);

int main()
{
#ifdef _WIN32
    if (!initialize_winsock())
    {
        fprintf(stderr, "Failed to initialize Winsock: %d.\n", GETSOCKETERROR());
        return EXIT_FAILURE;
    }
#endif

    SOCKET server_socket;
    if (!create_socket(&server_socket))
    {
        fprintf(stderr, "Failed to create socket: %d.\n", GETSOCKETERROR());
        cleanup_and_exit(server_socket, NULL, EXIT_FAILURE);
    }

    NetAddress address;
    EAddressResult address_result = set_address(&address, "0.0.0.0", 80);
    if (address_result != ADDRESS_SUCCESS)
    {
        switch (address_result)
        {
        case ADDRESS_ERR_INVALID_IP:
            fprintf(stderr, "Failed to set IP address: Invalid IP format.\n");
            break;

        case ADDRESS_ERR_DNS_RESOLUTION_FAILED:
            fprintf(stderr, "Failed to resolve DNS: Unable to resolve the hostname.\n");
            break;
        }

        cleanup_and_exit(server_socket, NULL, EXIT_FAILURE);
    }

    if (!bind_socket(server_socket, &address))
    {
        fprintf(stderr, "Failed to bind socket: %d.\n", GETSOCKETERROR());
        cleanup_and_exit(server_socket, NULL, EXIT_FAILURE);
    }

    if (!listen_socket(server_socket))
    {
        fprintf(stderr, "Failed to listen socket: %d.\n", GETSOCKETERROR());
        cleanup_and_exit(server_socket, NULL, EXIT_FAILURE);
    }

    printf("Waiting connections...\n");

    LPFDWATCH main_fdw = fdwatch_new(4096);
    if (!main_fdw)
    {
        fprintf(stderr, "Failed to create fdwatch.\n");
        cleanup_and_exit(server_socket, NULL, EXIT_FAILURE);
    }

    fdwatch_add_fd(main_fdw, server_socket, NULL, FDW_READ);

    while (1)
    {
        int num_events, event_idx;
        if ((num_events = fdwatch(main_fdw, 0)) < 0)
            break;

        CLIENT_DATA_POINTER client_data;

        for (event_idx = 0; event_idx < num_events; ++event_idx)
        {
            client_data = (CLIENT_DATA_POINTER)fdwatch_get_client_data(main_fdw, event_idx);

            if (!client_data)
            {
                if (FDW_READ == fdwatch_check_event(main_fdw, server_socket, event_idx))
                    process_new_connection(server_socket, main_fdw, event_idx);

                continue;
            }

            int event = fdwatch_check_event(main_fdw, client_data->socket, event_idx);

            switch (event)
            {
            case FDW_READ:
            {
                printf("Trying to recv data..\n");
                process_client_read(main_fdw, client_data);
            }
            break;

            case FDW_WRITE:
            {
                printf("Trying to send data..\n");
                process_client_write(main_fdw, client_data);
            }
            break;

            case FDW_EOF:
            default:
                fprintf(stderr, "fdwatch_check_event returned unknown %d, socket = %lld\n", event, client_data->socket);
                close_client_session(main_fdw, client_data->socket, client_data);
                break;
            }
        }
    }

    cleanup_and_exit(server_socket, &main_fdw, EXIT_SUCCESS);
    return 0;
}

int cleanup_and_exit(SOCKET server_socket, LPFDWATCH *main_fdw, int exit_code)
{
    printf("\n...Cleanup resources...\n");

    if (server_socket != INVALID_SOCKET)
        close_socket(server_socket);

    if (main_fdw && *main_fdw)
        fdwatch_delete(*main_fdw);

#ifdef _WIN32
    cleanup_winsock();
#endif

    printf("\n...Finished...\n");

    printf("Press Enter to continue...");
    getchar();

    exit(exit_code);
}
