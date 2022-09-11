
import socket

def init_connection(target_ip):
    '''Connect to the testing platform
    '''
    sok = socket.socket()
    #sok.settimeout(1)

    port_rx = 2223
    try:
        sok.connect((target_ip, port_rx))
    except socket.timeout:
        print("Connection timeout")
        return sok, -1
    except socket.error:
        print(f"Connection refused on {target_ip}:{port_rx}")
        return sok, -1
    except:
        return sok, -1

    print("Connection to the testing platform established.")
    return sok, 0

def send_action(sok, action_list):
    '''Send action_list to the testing platform
    '''
    sendStr = action_list
    sok.send(bytes(action_list, encoding = 'utf-8'))

def wait_for_state(sok):
    '''Wait for states from the testing platform
    '''
    while True:
        msg = sok.recv(1024)
        content = msg.decode("utf-8")
        return content

        # if content.startswith("cache"):  
        #     return content