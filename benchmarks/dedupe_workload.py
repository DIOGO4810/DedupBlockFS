import json
import random
import os
from .benchmark_state import State

def get_config():
    """
    Reads the configuration from config.json and validates it.
    """
    with open("config.json") as f:
        text = f.read()
        config = json.loads(text)
    total_pcnt = config["write_pcnt"] + config["read_pcnt"] + config["unlink_pcnt"]
    if total_pcnt != 100:
        print("Operation percentages don't add up to 100")
        return None
    if config["dup_pcnt"] > 100 or config["dup_pcnt"] < 0:
        print("Invalid duplicate percentage")
        return None
    if config["block_range"][0] < 0 or config["block_range"][1] < config["block_range"][0]:
        print("Invalid range limits")
        return None
    if config["num_ops"] <= 0:
        print("Number of operations must be greater than 0")
        return None
    if config["num_files"] <= 0:
        print("Number of files must be greater than 0")
        return None
    if config["num_unique_dup_blocks"] <= 0:
        print("Number of unique duplicate blocks must be greater than 0")
        return None
    return config


def write(state: State):
    """
    Executes a write operation in a random file.
    Appends a random number (between the configured range) of blocks, each of them with the
    configured probability of being duplicate.
    """

    # first build the whole request in a single buffer
    
    num_blocks = state.get_num_blocks()

    for i in range(num_blocks):

        dup = state.dup_or_not()

        if dup:
            state.append_dup_block(i)
        else:
            state.append_unique_block(i)

    # now execute the write operation at once
    fd = state.get_random_fd()
    buf = state.get_buffer(num_blocks*4096)
    os.write(fd, buf)


def read(state: State):
    """Execute a read operation."""


def unlink(state: State):
    """Execute an unlink operation."""


def workload(state: State):
    print("Workload started...")
    random.seed(42)
    for _ in range(state.num_ops):
        operation_rand = random.randint(0, 100)
        
        write_threshold = state.write_pcnt
        read_threshold = write_threshold + state.read_pcnt
        
        if operation_rand < write_threshold:
            write(state)
        elif operation_rand < read_threshold:
            read(state)
        else:
            unlink(state)

    print("Workload done")


def main():

    config = get_config()
    if config is None:
        print("Invalid configuration")
        quit()
    print("Configuration is valid!\nProceeding with the benchmark...")

    state = State(config)
    workload(state)
    state.free()


if __name__ == '__main__':
    main()
