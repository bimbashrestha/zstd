import os

# command: (graph, indegree)
GRAPHS = {
    "sequential": (
        {1: [2], 2: [3], 3: [4], 4: [5], 5: []},
        [0, 0, 1, 1, 1, 1],
    ),
    "crew": (
        {1: [2, 3, 4, 5, 6], 2: [7], 3: [7], 4: [7], 5: [7], 6: [7], 7: []},
        [0, 0, 1, 1, 1, 1, 1, 5],
    ),
    "shifted": (
        {
            1: [2],
            2: [3, 6],
            3: [4, 7],
            4: [],
            5: [6],
            6: [7, 10],
            7: [8, 11],
            8: [],
            9: [10],
            10: [11],
            11: [],
        },
        [0, 0, 1, 1, 1, 0, 2, 2, 1, 0, 2, 2],
    ),
}
THREAD_COUNTS = [1, 2, 4]

def execute(command):
    os.system("{} &> tmp".format(command))
    with open("tmp", "r") as f:
        res = f.read()
    os.system("rm -rf tmp")
    return res

def getAllTopoSorts(G, indegree):
    allTopoSorts = []
    def helper(G, visited, indegree, stack):
        done = False
        for i in G:
            if not visited[i] and indegree[i] == 0:
                visited[i] = True
                stack.append(i)
                for adj in G[i]:
                    indegree[adj] -= 1
                helper(G, visited, indegree, stack)
                visited[i] = False
                del stack[-1]
                for adj in G[i]:
                    indegree[adj] += 1
                done = True
        if not done:
            allTopoSorts.append(list(stack))
    visited = [False] * (len(G) + 1)
    helper(G, visited, indegree, [])
    return allTopoSorts

def checkAllTopoSorts(allTopoSortsList, threads):
    for allTopoSorts, (command, (graph, indegree)) in zip(allTopoSortsList, GRAPHS.items()):
        runs = [[int(x) for x in execute("./deppool-test {} -{}".format(threads, command)).split(" ") if x.isdigit()] for _ in range(10)]
        assert sum(r in allTopoSorts for r in runs) == len(runs)
        print("[pass][{}][threads={}] all runs valid topo sorts".format(command, threads))

if __name__ == "__main__":
    allTopoSortsList = [getAllTopoSorts(graph, indegree) for graph, indegree in GRAPHS.values()]
    for threadCount in THREAD_COUNTS:
        checkAllTopoSorts(allTopoSortsList, threadCount)
